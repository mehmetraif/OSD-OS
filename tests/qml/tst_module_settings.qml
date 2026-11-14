import QtQuick
import QtTest

// A module's settings page, views/ModuleSettings.qml, against a stand-in for
// AppCore: rows usable only while another row has a value (manifest
// "shown_with", as the Arcade module's rows for its Arcade screen are), an
// unset one by its default, there all the time but dimmed while that row
// keeps them back, the help line saying why, ◄ ► and select leaving them as
// they are; a toggle's value as ON or OFF, and a row whose own row is dimmed
// dimmed too, and one usable with any of several;
// a slider and its tape bar; a value that couldn't be saved left as it was. A
// multiselect_submenu row's page, views/MultiSelectSettings.qml, with its
// headings. A group's page, with another module's rows under its name, saved
// in that module.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    // The layout's frame and its pane (Main.qml): the column's icons as a
    // view hands them over, and where a view's rows go.
    property var frameIcons: []
    property int frameIndex: 0
    property bool frameActive: false
    property bool framed: false
    readonly property real paneX: framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: framed ? sw * 0.675 : sw * 0.76875
    function setFrame(icons, index, active, tall) { frameIcons = icons; frameIndex = index; frameActive = active }
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
    // The theme in force (Settings → Look → Theme), as AppCore::theme() reads
    // it: {} for none.
    property var theme: ({})

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
        // How many times config.json was written, and what is held back
        // from it: a slider's steps (save_setting_soon()), each its value
        // at once, written together by flush_settings() or the next save.
        // Should that write fail (saveFails), the settings are as they were
        // before the steps, and settingsNotSaved() says so.
        property int writes: 0
        property var held: []
        signal settingsNotSaved()
        function put(module, key, value) {
            var v = Object.assign({}, module === "com.osdos.other" ? otherValues : values)
            if (value === undefined)
                delete v[key]
            else
                v[key] = value
            if (module === "com.osdos.other")
                otherValues = v
            else
                values = v
        }
        function save_setting(module, key, value) {
            if (saveFails)
                return false
            put(module, key, value)
            writes++
            held = []
            return true
        }
        function save_setting_soon(module, key, value) {
            if (!held.some(function(h) { return h.module === module && h.key === key })) {
                var saved = module === "com.osdos.other" ? otherValues : values
                held = held.concat([{ module: module, key: key, before: saved[key] }])
            }
            put(module, key, value)
            return true
        }
        function flush_settings() {
            if (held.length === 0)
                return true
            var lost = held
            held = []
            if (!saveFails) {
                writes++
                return true
            }
            for (var i = 0; i < lost.length; i++)
                put(lost[i].module, lost[i].key, lost[i].before)
            settingsNotSaved()
            return false
        }
        function get_module_settings_schema(id) { return id === "com.osdos.other" ? otherSchema : schema }

        // The file picker's (views/FilePicker.qml): the places, and a
        // folder's entries (folders: path -> entries), read off the app's
        // thread: asked for (asked), nothing until the test hands them over
        // (deliver()), then folderEntriesReady. folderEntries() is the read
        // on the app's thread, counted. With cacheKept, what was handed over
        // before the picker opened is in at once. Kept in `picks`, changed in
        // place: as AppCore's own, these calls change no property a binding
        // follows.
        property var folders: ({})
        readonly property var picks: ({ asked: [], arrived: {}, readsOnThread: 0, cacheKept: false })
        signal folderEntriesReady(string path)
        function filePlaces() {
            return [{ name: "Home", path: "/home/me" }, { name: "Media", path: "/media" }, { name: "Root", path: "/" }]
        }
        function listed(path, types) {
            return (folders[path] || []).filter(function(entry) {
                return entry.isFolder || types.indexOf(entry.name.replace(/^.*\./, "")) >= 0
            }).map(function(entry) { return Object.assign({}, entry) })
        }
        function clearFolderCache() {
            if (!picks.cacheKept)
                picks.arrived = {}
        }
        function requestFolderEntries(path, types) {
            if (picks.arrived[path])
                return listed(path, types)
            if (picks.asked.indexOf(path) < 0)
                picks.asked.push(path)
            return undefined
        }
        function folderEntries(path, types) {
            picks.readsOnThread++
            return listed(path, types)
        }
        function deliver(path) {
            picks.arrived[path] = true
            folderEntriesReady(path)
        }
    }

    // The pages the view opens (navigateTo), as the router would.
    property var navigations: []

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params }])
        }
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
            appCore.writes = 0
            appCore.held = []
            appCore.folders = {}
            appCore.picks.asked = []
            appCore.picks.arrived = {}
            appCore.picks.readsOnThread = 0
            appCore.picks.cacheKept = false
            root.navigations = []
            root.helpLine = true
            root.theme = {}
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
        // The rows that can be used now, by their keys; the others are
        // dimmed (_off), their reason this.
        function usable(page) {
            return page.schemaItems.filter(function(item) { return item.type !== "section" && !item._off })
                       .map(function(item) { return item.key })
        }
        function off(page, key) {
            var item = page.schemaItems.filter(function(i) { return i.key === key })[0]
            return item ? (item._off || "") : "?"
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
        // The MenuRow drawn for the line at index: the list's delegate, or
        // the one in it (a slider's line has its bar beside it).
        function menuRow(item, index) {
            if (item.itemAtIndex !== undefined) {
                var d = item.itemAtIndex(index)
                if (!d)
                    return null
                if (d.opens !== undefined)
                    return d
                for (var j = 0; j < d.children.length; j++) {
                    if (d.children[j].opens !== undefined)
                        return d.children[j]
                }
                return null
            }
            for (var i = 0; i < item.children.length; i++) {
                var found = menuRow(item.children[i], index)
                if (found)
                    return found
            }
            return null
        }

        // A toggle's and a list's value stand between the ◄ and ► that
        // change them; a submenu's line ends in a ►, and ► opens it as select
        // does; a slider's line has neither (its bar has the arrows).
        function test_marks() {
            appCore.values = { screen: "Arcade" }
            appCore.schema = arcadeRows.concat([
                { key: "solidity", label: "Info Background", type: "slider", default: 60 }
            ])
            var page = open()
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video", "solidity"])
            var enabled = menuRow(page, 0)
            verify(enabled.changes)
            verify(!enabled.opens)
            verify(menuRow(page, 1).changes)
            var look = menuRow(page, 3)
            verify(look.opens)
            verify(!look.changes)
            var slider = menuRow(page, 5)
            verify(!slider.changes)
            verify(!slider.opens)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "look")
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/ModuleSettings.qml")
            compare(root.navigations[0].params.submenu, "look")
            // ► on the toggle flips it, opening nothing.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "enabled")
            keyClick(Qt.Key_Right)
            compare(appCore.values.enabled, true)
            compare(root.navigations.length, 1)
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
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video", undefined, "enabled", "mode"])
            compare(usable(page), ["enabled", "screen", "video", "enabled"])
            compare(page.schemaItems[5].type, "section")
            compare(page.schemaItems[5].label, "Other")
            verify(findText(page, "Other") !== null)
            compare(page.currentDisplayValue(page.schemaItems[0]), "ON")
            compare(page.currentDisplayValue(page.schemaItems[6]), "OFF")
            compare(page.schemaItems[7]._off, "Only with Enabled on")
            for (var i = 0; i < 5; ++i)
                keyClick(Qt.Key_Down)
            compare(cursorIndex(page), 6)
            keyClick(Qt.Key_Right)
            compare(appCore.otherValues.enabled, true)
            compare(appCore.values.enabled, true)
            compare(page.currentDisplayValue(page.schemaItems[6]), "ON")
            compare(usable(page), ["enabled", "screen", "video", "enabled", "mode"])
            compare(cursorIndex(page), 6)
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
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
        }

        // Unset, Screen is its default, Original: the Arcade screen's rows are
        // there but dimmed, saying for what, and ◄ ► and select leave them
        // as they are; their choices are asked for at once, so they show
        // what they hold. Arcade makes them usable, the cursor staying on
        // Screen; Original dims them again, what was set in them kept.
        function test_shownWith() {
            var page = open()
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(usable(page), ["enabled", "screen", "video"])
            compare(off(page, "start_filter"), "Only with Screen on Arcade")
            compare(appCore.actions, ["startFilterOptions"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "start_filter")
            var row = menuRow(page, 2)
            verify(row.dimmed)
            verify(!row.changes)
            verify(findText(page, "Only with Screen on Arcade") !== null)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(appCore.values.start_filter, undefined)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "look")
            verify(!menuRow(page, 3).opens)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 0)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "screen")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, "Arcade")
            compare(usable(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(cursor(page), "screen")
            compare(appCore.actions, ["startFilterOptions"])
            verify(!menuRow(page, 2).dimmed)
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
            compare(usable(page), ["enabled", "screen", "video"])
            compare(cursor(page), "screen")
            compare(appCore.values.start_filter, "favorites")
            compare(page.currentDisplayValue(page.schemaItems[2]), "Favorites")
            // Saved as Arcade before the page opened: usable from the start,
            // its choices asked for with the rest.
            loader.source = ""
            appCore.values = { screen: "Arcade" }
            appCore.actions = []
            page = open()
            compare(usable(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(appCore.actions, ["startFilterOptions"])
        }

        // A value that couldn't be saved (a full card): the row stays on what
        // is saved, its rows with it; saved, it changes.
        // A backend that puts a row's value back (a profile switch that
        // failed) sends its choices again: the row shows the saved value.
        function test_valueReadAgainWithChoices() {
            appCore.values = { screen: "Arcade", start_filter: "favorites" }
            var page = open()
            var choices = [{ id: "all", label: "All Games" }, { id: "favorites", label: "Favorites" }]
            appCore.dynamicOptionsReady("com.osdos.test", "start_filter", choices)
            compare(page.currentDisplayValue(page.schemaItems[2]), "Favorites")
            appCore.values = { screen: "Arcade", start_filter: "all" }
            compare(page.currentDisplayValue(page.schemaItems[2]), "Favorites")
            appCore.dynamicOptionsReady("com.osdos.test", "start_filter", choices)
            compare(page.currentDisplayValue(page.schemaItems[2]), "All Games")
        }

        // A "text" row is typed on the on-screen keyboard: select or ► opens
        // it on what is saved, OK keeps what was typed (without the spaces
        // at its ends) and calls the row's apply slot, back leaves it; a
        // "secret" one never shows what it holds.
        function test_textRow() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "server_url", label: "Server Address", type: "text", default: "", max_length: 64,
                  apply_slot: "addressChanged" },
                { key: "api_key", label: "Key", type: "text", secret: true, default: "" }
            ]
            var page = open()
            var keyboard = null
            for (var i = 0; i < page.children.length && !keyboard; i++) {
                if (page.children[i].objectName === "keyboard")
                    keyboard = page.children[i]
            }
            verify(keyboard !== null)
            compare(page.currentDisplayValue(page.schemaItems[1]), "Not Set")
            verify(page.opens(page.schemaItems[1]))
            keyClick(Qt.Key_Down)
            compare(cursor(page), "server_url")
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            compare(keyboard.title, "Server Address")
            compare(keyboard.maxLength, 64)
            verify(!keyboard.masked)
            keyboard.accepted("  http://nas:8096 ")
            compare(appCore.values.server_url, "http://nas:8096")
            compare(page.currentDisplayValue(page.schemaItems[1]), "http://nas:8096")
            compare(appCore.actions, ["addressChanged"])
            // Opened again on what is saved; back keeps it.
            keyClick(Qt.Key_Right)
            verify(keyboard.visible)
            compare(keyboard.text, "http://nas:8096")
            keyboard.canceled()
            compare(appCore.values.server_url, "http://nas:8096")
            // A key: typed hidden, and only said to be set.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(keyboard.masked)
            keyboard.accepted("Key456example")
            compare(appCore.values.api_key, "Key456example")
            compare(page.currentDisplayValue(page.schemaItems[2]), "Set")
            // Nothing typed unsets the row.
            keyClick(Qt.Key_Return)
            keyboard.accepted("")
            compare(appCore.values.api_key, "")
            compare(page.currentDisplayValue(page.schemaItems[2]), "Not Set")
        }

        // A list of typed lines (text_list): its count on the row, its own
        // page from it, there a line added on the keyboard, changed, not
        // listed twice, taken out, and no more than the row takes.
        function test_textList() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "channels", label: "Channels", type: "text_list", max_length: 30, max_lines: 3,
                  description: "A line each", apply_slot: "channelsChanged" }
            ]
            appCore.values = { channels: ["UCone", "UCtwo"] }
            var page = open()
            compare(page.currentDisplayValue(page.schemaItems[1]), "2")
            verify(page.opens(page.schemaItems[1]))
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/TextListSettings.qml")
            var params = root.navigations[0].params
            compare(params.settingKey, "channels")
            compare(params.maxLines, 3)
            appCore.values = {}
            compare(page.currentDisplayValue(page.schemaItems[1]), "2")
            loader.source = ""
            appCore.values = {}
            page = open()
            compare(page.currentDisplayValue(page.schemaItems[1]), "None")
            loader.source = ""

            appCore.values = { channels: ["UCone", " ", "UCtwo"] }
            loader.setSource(Qt.resolvedUrl("../../views/TextListSettings.qml"), { navParams: params })
            var list = loader.item
            verify(list !== null, "loaded TextListSettings.qml")
            list.forceActiveFocus()
            var keyboard = null, options = null, lines = null
            for (var i = 0; i < list.children.length; i++) {
                var child = list.children[i]
                if (child.objectName === "keyboard") keyboard = child
                if (child.objectName === "options") options = child
                if (child.objectName === "lines") lines = child
            }
            verify(keyboard && options && lines)
            compare(list.lines, ["UCone", "UCtwo"])
            compare(lines.count, 3)
            // ADD, the last row: a line typed, kept at the foot, the cursor
            // left on ADD.
            lines.currentIndex = 2
            verify(lines.onAdd)
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            compare(keyboard.title, "Channels")
            compare(keyboard.maxLength, 30)
            keyboard.accepted("  UCthree ")
            verify(!keyboard.visible)
            compare(appCore.values.channels, ["UCone", "UCtwo", "UCthree"])
            compare(appCore.actions, ["channelsChanged"])
            compare(lines.count, 4)
            verify(lines.onAdd)
            // Full: ADD opens nothing.
            verify(list.full)
            keyClick(Qt.Key_Return)
            verify(!keyboard.visible)
            // A line: change it, or take it out.
            lines.currentIndex = 0
            keyClick(Qt.Key_Return)
            verify(options.visible)
            compare(options.subtitleText, "UCone")
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            compare(keyboard.text, "UCone")
            // One already there isn't listed twice: the line goes.
            keyboard.accepted("UCtwo")
            compare(appCore.values.channels, ["UCtwo", "UCthree"])
            compare(lines.currentIndex, 0)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            keyboard.accepted("UCfirst")
            compare(appCore.values.channels, ["UCfirst", "UCthree"])
            compare(lines.currentIndex, 0)
            // Remove, the second answer.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!options.visible)
            compare(appCore.values.channels, ["UCthree"])
            compare(lines.currentIndex, 0)
            // Not saved (a full card): the page stays on what is saved.
            appCore.saveFails = true
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(list.lines, ["UCthree"])
            // Back leaves the page.
            var left = 0
            list.goBack.connect(function() { left++ })
            keyClick(Qt.Key_Escape)
            compare(left, 1)
        }

        function test_unsaved() {
            var page = open()
            keyClick(Qt.Key_Down)
            appCore.saveFails = true
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, undefined)
            compare(page.currentDisplayValue(page.schemaItems[1]), "Original")
            compare(usable(page), ["enabled", "screen", "video"])
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Right)
            compare(page.currentDisplayValue(page.schemaItems[0]), "OFF")
            appCore.saveFails = false
            keyClick(Qt.Key_Right)
            compare(page.currentDisplayValue(page.schemaItems[0]), "ON")
            compare(appCore.values.enabled, true)
        }

        // A toggle's rows follow it as ON or OFF, unset as its default and
        // saved as true or false; a row whose own row is dimmed is dimmed
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
            compare(usable(page), ["music", "volume", "loud"])
            keyClick(Qt.Key_Right)
            compare(appCore.values.music, false)
            compare(keys(page), ["music", "volume", "loud"])
            compare(usable(page), ["music"])
            compare(off(page, "volume"), "Only with Music on")
            compare(off(page, "loud"), "Only with Volume on High")
            keyClick(Qt.Key_Right)
            compare(appCore.values.music, true)
            compare(usable(page), ["music", "volume", "loud"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.volume, "Low")
            compare(usable(page), ["music", "volume"])
            compare(cursor(page), "volume")
        }

        // A row usable with a list of rows' values (the Arcade module's
        // Online Name, with Online Play or LAN Play): usable while any of
        // them has its value, dimmed once none has. The list as a JS array,
        // and as the app hands a manifest's list over from C++: a sequence,
        // which isn't one (Array.isArray() says no).
        function test_shownWithAny_data() {
            var either = [{ key: "online", value: "On" }, { key: "lan", value: "On" }]
            return [{ tag: "array", shownWith: either },
                    { tag: "sequence", shownWith: { length: 2, 0: either[0], 1: either[1] } }]
        }
        function test_shownWithAny(data) {
            appCore.schema = [
                { key: "online", label: "Online Play", type: "list_single", options: ["Off", "On"], default: "Off" },
                { key: "lan", label: "LAN Play", type: "list_single", options: ["Off", "On"], default: "Off" },
                { key: "name", label: "Online Name", type: "list_single", options: ["A", "B"], default: "A",
                  shown_with: data.shownWith }
            ]
            var page = open()
            compare(keys(page), ["online", "lan", "name"])
            compare(usable(page), ["online", "lan"])
            compare(off(page, "name"), "Only with Online Play on or LAN Play on")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.lan, "On")
            compare(usable(page), ["online", "lan", "name"])
            compare(cursor(page), "lan")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Right)
            compare(usable(page), ["online", "lan", "name"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Left)
            compare(appCore.values.lan, "Off")
            compare(usable(page), ["online", "lan", "name"])
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Left)
            compare(appCore.values.online, "Off")
            compare(usable(page), ["online", "lan"])
            compare(cursor(page), "online")
        }

        // A row kept back while something holds (manifest "shown_unless",
        // "shown_with" turned round): by the theme in force ({ theme, value },
        // as the Arcade screen's List Style, Border and Info Background are
        // in the Orb look), in the condition's own words or ones made of it;
        // or by another row's value. Dimmed, ◄ ► leave it as it is; a row
        // that "shown_with" keeps back too says that first. With another
        // theme, or none, it is a row like the others.
        function test_shownUnless() {
            appCore.schema = [
                { key: "screen", label: "Screen", type: "list_single", options: ["Original", "Arcade"],
                  default: "Arcade" },
                { key: "list_style", label: "List Style", type: "list_single", options: ["Straight", "Curved"],
                  default: "Curved", shown_with: { key: "screen", value: "Arcade" },
                  shown_unless: { theme: "arcade", value: "orb", reason: "Not with a theme in the Orb look" } },
                { key: "border", label: "Border", type: "toggle", default: "ON",
                  shown_unless: { theme: "arcade", value: "orb" } },
                { key: "tree_sort", label: "Tree Sort", type: "list_single", options: ["Name", "Year"],
                  default: "Name", shown_unless: [{ key: "screen", value: "Arcade" }] },
                { key: "frame", label: "Frame", type: "toggle", default: "ON",
                  shown_with: { key: "border", value: "ON" } }
            ]
            root.theme = { name: "Orb", arcade: "orb" }
            var page = open()
            compare(keys(page), ["screen", "list_style", "border", "tree_sort", "frame"])
            compare(usable(page), ["screen"])
            compare(off(page, "list_style"), "Not with a theme in the Orb look")
            compare(off(page, "border"), "Not with the theme's arcade orb")
            compare(off(page, "tree_sort"), "Not with Screen on Arcade")
            // A row that goes with one kept back is kept back with it.
            compare(off(page, "frame"), "Only with Border on")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "list_style")
            verify(menuRow(page, 1).dimmed)
            verify(!menuRow(page, 1).changes)
            verify(findText(page, "Not with a theme in the Orb look") !== null)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(appCore.values.list_style, undefined)
            // Screen on Original: List Style waits for its Screen first, and
            // Tree Sort is the tree's own.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Left)
            compare(appCore.values.screen, "Original")
            compare(off(page, "list_style"), "Only with Screen on Arcade")
            compare(usable(page), ["screen", "tree_sort"])
            compare(cursor(page), "screen")
            // A theme without that look, and none.
            var themes = [{ name: "Late Night" }, {}]
            for (var i = 0; i < themes.length; i++) {
                loader.source = ""
                appCore.values = {}
                root.theme = themes[i]
                page = open()
                compare(usable(page), ["screen", "list_style", "border", "frame"])
                keyClick(Qt.Key_Down)
                keyClick(Qt.Key_Left)
                compare(appCore.values.list_style, "Straight")
            }
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

        // A slider's steps are its value at once, on the page and in the
        // settings, but config.json isn't written at each: they are held
        // (save_setting_soon()) and written once, as the page is left; a
        // toggle or a list is written at once, as ever, the steps with it.
        function test_sliderWrittenOnce() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "ground", label: "Info Background", type: "slider", default: 100, step: 10 }
            ]
            var page = open()
            keyClick(Qt.Key_Down)
            for (var ground = 90; ground >= 50; ground -= 10) {
                keyClick(Qt.Key_Left)
                compare(appCore.values.ground, ground)
                compare(page.valueOf(page.schemaItems[1]), ground)
            }
            compare(appCore.writes, 0)
            compare(appCore.held.length, 1)
            loader.source = ""
            compare(appCore.writes, 1)
            compare(appCore.held.length, 0)
            compare(appCore.values.ground, 50)

            page = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.writes, 1)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(appCore.values.enabled, true)
            compare(appCore.writes, 2)
            compare(appCore.held.length, 0)
            loader.source = ""
            compare(appCore.writes, 2)
        }

        // Steps the card can't take: once the write has failed (AppCore's
        // settingsNotSaved) the slider is back on what is saved, the cursor
        // still on its row.
        function test_sliderNotSaved() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "ground", label: "Info Background", type: "slider", default: 100, step: 10 }
            ]
            appCore.values = { ground: 80 }
            var page = open()
            keyClick(Qt.Key_Down)
            appCore.saveFails = true
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(page.valueOf(page.schemaItems[1]), 60)
            // The steps at rest, as AppCore's timer has it.
            verify(!appCore.flush_settings())
            compare(appCore.values.ground, 80)
            compare(page.valueOf(page.schemaItems[1]), 80)
            compare(cursor(page), "ground")
            compare(appCore.writes, 0)
            appCore.saveFails = false
            keyClick(Qt.Key_Right)
            compare(page.valueOf(page.schemaItems[1]), 90)
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

        // A multiselect_submenu row's page before its choices come, and with
        // none: Loading… until the backend answers; No Choices for an answer
        // with none, and for one that says why (a single { problem }), which
        // the help line says whatever Settings' Help Line is; nothing is
        // saved from such a line. Choices that come after take its place.
        function test_multiSelectNone() {
            appCore.schema = [{ key: "libraries", label: "Libraries", type: "multiselect_submenu",
                                options_source: "dynamic", options_slot: "getLibraries" }]
            loader.setSource(Qt.resolvedUrl("../../views/MultiSelectSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "libraries",
                                            settingLabel: "Libraries" } })
            var page = loader.item
            verify(page !== null, "loaded MultiSelectSettings.qml")
            page.forceActiveFocus()
            compare(appCore.actions, ["getLibraries"])
            verify(findText(page, "Loading…") !== null)
            verify(findText(page, "No Choices") === null)
            var line = menuRow(page, 0)
            verify(!line.changes)
            verify(!line.selected)
            compare(line.value, "")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Right)
            compare(appCore.values.libraries, undefined)

            // Another row's choices are not this page's answer.
            appCore.dynamicOptionsReady("com.osdos.test", "server", [])
            verify(findText(page, "Loading…") !== null)

            // The server out of reach.
            root.helpLine = false
            appCore.dynamicOptionsReady("com.osdos.test", "libraries", [{ problem: "The server could not be reached" }])
            verify(findText(page, "Loading…") === null)
            verify(findText(page, "No Choices") !== null)
            var why = findText(page, "The server could not be reached")
            verify(why !== null)
            var box = why
            while (box !== null && box.shown === undefined)
                box = box.parent
            verify(box !== null)
            verify(box.shown, "said with Help Line off too")
            compare(box.opacity, 1)
            root.helpLine = true
            compare(page.options.length, 0)
            keyClick(Qt.Key_Return)
            compare(appCore.values.libraries, undefined)

            // A server with none to offer: no reason to give.
            appCore.dynamicOptionsReady("com.osdos.test", "libraries", [])
            verify(findText(page, "No Choices") !== null)
            verify(findText(page, "The server could not be reached") === null)

            // Asked again and answered: the choices.
            appCore.dynamicOptionsReady("com.osdos.test", "libraries", [{ id: "1", label: "FILMS" }])
            verify(findText(page, "No Choices") === null)
            verify(findText(page, "FILMS") !== null)
            verify(menuRow(page, 0).selected)
            keyClick(Qt.Key_Return)
            compare(appCore.values.libraries, { "1": false })
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

        // The file picker (views/FilePicker.qml), as a directory_browser row
        // opens it: a drive's folders, and the picker's tree and its open
        // folders' paths.
        function folder(name, path) { return { name: name, path: path, isFolder: true } }
        function file(name, path) { return { name: name, path: path, isFolder: false } }
        readonly property var driveFolders: ({
            "/media": [folder("card", "/media/card"), folder("usb", "/media/usb")],
            "/media/usb": [folder("films", "/media/usb/films"), folder("music", "/media/usb/music"),
                           file("logo.png", "/media/usb/logo.png"), file("notes.txt", "/media/usb/notes.txt")],
            "/media/usb/films": [folder("drama", "/media/usb/films/drama"), folder("horror", "/media/usb/films/horror"),
                                 file("a.mkv", "/media/usb/films/a.mkv")],
            "/media/usb/films/horror": [folder("old", "/media/usb/films/horror/old")]
        })
        function openPicker(params) {
            loader.setSource(Qt.resolvedUrl("../../views/FilePicker.qml"),
                             { navParams: Object.assign({ moduleId: "com.osdos.test", settingKey: "folder",
                                                          label: "Folder" }, params) })
            verify(loader.item !== null, "loaded FilePicker.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        function treeOf(picker) {
            for (var i = 0; i < picker.children.length; i++) {
                if (picker.children[i].trailState !== undefined)
                    return picker.children[i]
            }
            return null
        }
        function openFolders(tree) {
            return tree.trailState().map(function(column) { return column.path })
        }
        function cursorIn(tree, path) {
            var column = tree.trailState().filter(function(c) { return c.path === path })[0]
            return column ? column.sel : -1
        }

        // The picker opens down to the folder chosen now without reading a
        // folder on the app's thread: each folder on the way is asked for and
        // opened on trust, saying loading… until it is in, whatever order
        // they come in, and then has its cursor on the way on. Select on USE
        // THIS FOLDER picks the folder.
        function test_filePickerOpensWithoutWaiting() {
            appCore.folders = driveFolders
            var picker = openPicker({ currentPath: "/media/usb/films/horror", defaultLabel: "Default Folder" })
            var tree = treeOf(picker)
            verify(tree !== null)
            compare(appCore.picks.readsOnThread, 0)
            compare(openFolders(tree), ["places", "/media", "/media/usb", "/media/usb/films", "/media/usb/films/horror"])
            // The places: the default, Home, Media, Root.
            compare(cursorIn(tree, "places"), 2)
            var onTheWay = ["/media", "/media/usb", "/media/usb/films", "/media/usb/films/horror"]
            for (var i = 0; i < onTheWay.length; i++)
                verify(appCore.picks.asked.indexOf(onTheWay[i]) >= 0, onTheWay[i] + " asked for")
            verify(findText(picker, "loading…") !== null)
            compare(tree.folderName, "horror")
            compare(tree.currentEntry, null)

            // The folder chosen comes in first: the cursor on USE THIS FOLDER.
            appCore.deliver("/media/usb/films/horror")
            compare(tree.currentEntry.kind, "choose")
            compare(tree.currentEntry.target, "/media/usb/films/horror")
            // Then one in the middle: its cursor on the folder that leads on.
            appCore.deliver("/media/usb")
            compare(cursorIn(tree, "/media/usb"), 1)
            // The cursor moved meanwhile stays where it was put: a folder
            // coming in later moves its own alone.
            keyClick(Qt.Key_Down)
            compare(tree.currentEntry.name, "old")
            appCore.deliver("/media/usb/films")
            compare(cursorIn(tree, "/media/usb/films"), 2)
            appCore.deliver("/media")
            compare(cursorIn(tree, "/media"), 2)
            compare(openFolders(tree), ["places", "/media", "/media/usb", "/media/usb/films", "/media/usb/films/horror"])
            compare(tree.currentEntry.name, "old")
            compare(appCore.picks.readsOnThread, 0)

            keyClick(Qt.Key_Up)
            var back = signalSpy.createObject(picker, { target: picker, signalName: "goBack" })
            keyClick(Qt.Key_Return)
            compare(appCore.values.folder, "/media/usb/films/horror")
            compare(back.count, 1)
        }

        // A folder on the way gone since (deleted, its drive taken out): once
        // the folder that held it is in, the folders opened on trust beyond
        // it close, and the tree stops at the last folder there is.
        function test_filePickerFolderGone() {
            appCore.folders = driveFolders
            var picker = openPicker({ currentPath: "/media/usb/gone/deeper" })
            var tree = treeOf(picker)
            compare(openFolders(tree), ["places", "/media", "/media/usb", "/media/usb/gone", "/media/usb/gone/deeper"])
            appCore.deliver("/media/usb/gone/deeper")
            appCore.deliver("/media/usb")
            compare(openFolders(tree), ["places", "/media", "/media/usb"])
            compare(tree.currentEntry.kind, "choose")
            compare(tree.currentEntry.target, "/media/usb")
            appCore.deliver("/media")
            compare(cursorIn(tree, "/media"), 2)
            compare(openFolders(tree), ["places", "/media", "/media/usb"])
            compare(appCore.picks.readsOnThread, 0)
        }

        // Picking a file: the tree opens down to its folder, and the cursor
        // goes on the file once the folder is in; select picks it.
        function test_filePickerFile() {
            appCore.folders = driveFolders
            var picker = openPicker({ settingKey: "logo", mode: "file", types: ["png"],
                                      currentPath: "/media/usb/logo.png", defaultLabel: "OSD/OS Logo" })
            var tree = treeOf(picker)
            compare(openFolders(tree), ["places", "/media", "/media/usb"])
            verify(findText(picker, "loading…") !== null)
            appCore.deliver("/media")
            appCore.deliver("/media/usb")
            // The folders, then the files of the types asked for.
            compare(tree.currentEntry.path, "/media/usb/logo.png")
            compare(cursorIn(tree, "/media/usb"), 2)
            compare(appCore.picks.readsOnThread, 0)
            keyClick(Qt.Key_Return)
            compare(appCore.values.logo, "/media/usb/logo.png")
        }

        // What is in already as the picker opens (nothing is, in the app,
        // which reads the folders afresh at each visit) has its cursor on the
        // way on at once, and only the rest is opened on trust.
        function test_filePickerFoldersInAlready() {
            appCore.folders = driveFolders
            appCore.picks.cacheKept = true
            appCore.picks.arrived = { "/media": true, "/media/usb": true }
            var picker = openPicker({ currentPath: "/media/usb/films/horror" })
            var tree = treeOf(picker)
            compare(openFolders(tree), ["places", "/media", "/media/usb", "/media/usb/films", "/media/usb/films/horror"])
            compare(cursorIn(tree, "/media"), 2)
            compare(cursorIn(tree, "/media/usb"), 1)
            appCore.deliver("/media/usb/films")
            compare(cursorIn(tree, "/media/usb/films"), 2)
            compare(appCore.picks.readsOnThread, 0)
        }

        Component {
            id: signalSpy
            SignalSpy {}
        }
    }
}
