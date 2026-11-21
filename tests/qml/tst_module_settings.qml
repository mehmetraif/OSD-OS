import QtQuick
import QtTest
import Components
import OSDOS.Testing

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
StandInRoot {
    id: root
    // White on black in every layout, and the frame's colours in force
    // whenever it is up.
    primaryColor: schemeInk
    surfaceColor: schemePaper
    cursorColor: primaryColor
    cursorInk: surfaceColor
    monitorInk: monitorFramed
    // The rows' room the same whether or not the Help Line shows.
    helpRoom: 0
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
        // The app's own settings (app.<key>), and AppCore saying one changed.
        property var appValues: ({})
        signal appSettingChanged(string key, string value)
        function get_setting(module, key) {
            if (module === "")
                return appValues[key]
            var saved = module === "com.osdos.other" ? otherValues : values
            var parts = key.split(".")
            return parts.length === 2 ? (saved[parts[0]] || {})[parts[1]] : saved[key]
        }
        function get_installed_modules() { return [{ id: "com.osdos.test", name: "Test" }, { id: "com.osdos.other", name: "Other" }] }
        // Who is signed in, and how often the page asked for that and for a
        // schema (counted in place: no binding follows them).
        property string authState: ""
        readonly property var calls: ({ schemas: 0, states: 0 })
        function get_module_auth_state(id) {
            calls.states++
            return authState
        }
        // A slot called on the module's backend; false where it has none
        // such (noSuchAction), as AppCore says.
        property bool noSuchAction: false
        function invoke_module_action(id, slot) {
            if (noSuchAction)
                return false
            actions = actions.concat([slot])
            return true
        }
        // How many times config.json was written, and what is held back
        // from it: a slider's steps (save_setting_soon()), each its value
        // at once, written together by flush_settings() or the next save.
        // Should that write fail (saveFails), the settings are as they were
        // before the steps, and settingsNotSaved() says so.
        property int writes: 0
        property var held: []
        signal settingsNotSaved()
        // A key in dot-notation ("libraries.somekey") is saved, and read,
        // as the somekey of libraries, as AppCore's putSetting() has it.
        function put(module, key, value) {
            var v = Object.assign({}, module === "com.osdos.other" ? otherValues : values)
            var parts = key.split(".")
            var at = v
            if (parts.length === 2) {
                at = Object.assign({}, typeof v[parts[0]] === "object" && v[parts[0]] ? v[parts[0]] : {})
                v[parts[0]] = at
                key = parts[1]
            }
            if (value === undefined)
                delete at[key]
            else
                at[key] = value
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
                held = held.concat([{ module: module, key: key, before: get_setting(module, key) }])
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
        // A module_view row's page: the module's own Root.qml.
        function module_entry_point(id) { return "modules/" + id + "/views/Root.qml" }
        function get_module_settings_schema(id) {
            calls.schemas++
            return id === "com.osdos.other" ? otherSchema : schema
        }

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
            root.menuLayout = "Standard"
            root.framed = false
            root.chrome.basePath = []
            appCore.values = {}
            appCore.actions = []
            appCore.schema = arcadeRows
            appCore.otherValues = {}
            appCore.appValues = {}
            appCore.otherSchema = []
            appCore.saveFails = false
            appCore.noSuchAction = false
            appCore.authState = ""
            appCore.calls.schemas = appCore.calls.states = 0
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
        // the one in it (a SettingRow's, which has a slider's bar under it).
        function menuRow(item, index) {
            if (item.itemAtIndex !== undefined) {
                var d = item.itemAtIndex(index)
                if (!d)
                    return null
                if (d.keepValue !== undefined)
                    return d
                for (var j = 0; j < d.children.length; j++) {
                    if (d.children[j].keepValue !== undefined)
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

        // The page's list (MenuList), for where it is scrolled to.
        function menuOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].contentY !== undefined && page.children[i].shownHeight !== undefined)
                    return page.children[i]
            }
            return null
        }

        // What dims a row changes (the row it goes with, what the server can
        // do, a sign-in): the rows dimmed or usable otherwise change in
        // place. No line of the list is made again, the list stays where it
        // was scrolled to with the cursor on its row, the help line follows,
        // and the schema isn't asked for again.
        function test_rowsFollowInPlace() {
            var rows = []
            for (var i = 0; i < 10; i++)
                rows.push({ key: "fill" + i, label: "Fill " + i, type: "toggle", default: "OFF" })
            rows.push({ key: "screen", label: "Screen", type: "list_single", options: ["Original", "Arcade"],
                        default: "Original" })
            rows.push({ key: "border", label: "Border", type: "toggle", default: "ON",
                        shown_with: { key: "screen", value: "Arcade" }, description: "A border round the list" })
            rows.push({ key: "skip", label: "Skip Intros", type: "toggle", default: "ON",
                        requires_capability: "mediasegments" })
            rows.push({ key: "server", label: "Server", type: "toggle", default: "ON", requires_auth: true })
            appCore.schema = rows
            var page = open()
            compare(appCore.calls.schemas, 1)
            compare(appCore.calls.states, 1)
            // To the list's foot, then back up to Screen, the last rows in view.
            for (i = 0; i < 13; i++)
                keyClick(Qt.Key_Down)
            for (i = 0; i < 3; i++)
                keyClick(Qt.Key_Up)
            compare(cursor(page), "screen")
            var menu = menuOf(page)
            verify(menu !== null)
            var scrolled = menu.contentY
            verify(scrolled > 0)
            var lines = [menuRow(page, 10), menuRow(page, 11), menuRow(page, 12), menuRow(page, 13)]
            for (i = 0; i < lines.length; i++)
                verify(lines[i] !== null, "line " + (10 + i) + " drawn")
            verify(lines[1].dimmed && lines[2].dimmed && lines[3].dimmed)
            var model = page.schemaItems
            // Each line still the one it was, and the list where it was.
            var kept = function(what) {
                for (var n = 0; n < lines.length; n++)
                    verify(menuRow(page, 10 + n) === lines[n], what + ": line " + (10 + n) + " not made again")
                verify(page.schemaItems === model, what + ": the list's rows not given anew")
                compare(menu.contentY, scrolled)
                compare(cursor(page), "screen")
                compare(appCore.calls.schemas, 1)
            }

            // The row Border goes with.
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, "Arcade")
            compare(off(page, "border"), "")
            verify(!lines[1].dimmed)
            verify(lines[1].changes)
            verify(lines[2].dimmed && lines[3].dimmed)
            kept("a row's value")

            // What the server can do.
            appCore.dynamicOptionsReady("com.osdos.test", "_capabilities", ["mediasegments"])
            compare(off(page, "skip"), "")
            verify(!lines[2].dimmed)
            verify(lines[3].dimmed)
            kept("the capabilities")

            // A sign-in: the state asked for again, not the schema.
            appCore.authState = "user"
            appCore.moduleAuthStateChanged("com.osdos.test")
            compare(off(page, "server"), "")
            verify(!lines[3].dimmed)
            compare(appCore.calls.states, 2)
            kept("a sign-in")

            // And back: dimmed again in place, the help line saying why.
            keyClick(Qt.Key_Left)
            compare(off(page, "border"), "Only with Screen on Arcade")
            verify(lines[1].dimmed)
            verify(!lines[1].changes)
            kept("the value back")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "border")
            verify(findText(page, "Only with Screen on Arcade") !== null)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Down)
            verify(findText(page, "Only with Screen on Arcade") === null)
            verify(findText(page, "A border round the list") !== null)
        }

        // A toggle's and a list's value stand between the ◄ and ► that
        // change them; a submenu's line ends in a ►, and ► opens it as select
        // does; a slider's line has neither (its bar has the arrows).
        // The help line shows the row's first sentence and the [ ] of the
        // value now on the row alone, following ◄ ►; a row without a value
        // (an action) shows its line whole.
        function test_helpFollowsTheValue() {
            appCore.schema = [
                { key: "mode", label: "Mode", type: "list_single", options: ["Slow", "Fast"], "default": "Slow",
                  description: "How it plays.\n[SLOW] Takes its time  [FAST] Hurries" },
                { key: "wipe", label: "Wipe", type: "action", slot: "wipe",
                  description: "Forgets it all. [SLOW] is no value of this row" }
            ]
            var page = open()
            var help = findChild(page, "helpLine")
            compare(help.value, "Slow")
            verify(help.shownText.indexOf("How it plays.") === 0, help.shownText)
            verify(help.shownText.indexOf("[SLOW] Takes its time") > 0, help.shownText)
            verify(help.shownText.indexOf("[FAST]") < 0, help.shownText)
            keyClick(Qt.Key_Right)
            tryCompare(help, "value", "Fast")
            verify(help.shownText.indexOf("[FAST] Hurries") > 0, help.shownText)
            verify(help.shownText.indexOf("[SLOW]") < 0, help.shownText)
            keyClick(Qt.Key_Down)
            compare(help.value, "")
            compare(help.shownText, help.text)
        }

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

        // A dynamic list that was a toggle once and is still saved as true
        // or false (Local Files' Shuffle Playback): the row is on the choice
        // that form is now (the choice's `old`), as the player takes it. It
        // shows that choice, ◄ ► step from it, the rows that go with its
        // value follow it, and a value changed in two steps (Broadcast) is
        // counted from it too.
        function test_savedAsAToggleOnce_data() {
            return [{ tag: "false", saved: false, label: "Never", id: "no", next: "ask", nextLabel: "Ask", before: "yes" },
                    { tag: "true", saved: true, label: "Always", id: "yes", next: "no", nextLabel: "Never", before: "ask" }]
        }
        function test_savedAsAToggleOnce(data) {
            appCore.schema = [
                { key: "shuffle", label: "Shuffle Playback", type: "list_single", options_source: "dynamic",
                  options_slot: "shuffleOptions", default: "ask" },
                { key: "order", label: "Order", type: "toggle", default: "ON",
                  shown_with: { key: "shuffle", value: "no" } }
            ]
            var choices = [{ id: "ask", label: "Ask" }, { id: "yes", label: "Always", old: true },
                           { id: "no", label: "Never", old: false }]
            // The page with the row saved in its old form, its choices in.
            var opened = function() {
                loader.source = ""
                appCore.values = { shuffle: data.saved }
                var page = open()
                appCore.dynamicOptionsReady("com.osdos.test", "shuffle", choices)
                return page
            }
            var page = opened()
            compare(page.currentDisplayValue(page.schemaItems[0]), data.label)
            compare(menuRow(page, 0).value, data.label)
            compare(page.valueOf(page.schemaItems[0]), data.id)
            compare(usable(page), data.id === "no" ? ["shuffle", "order"] : ["shuffle"])
            keyClick(Qt.Key_Right)
            compare(appCore.values.shuffle, data.next)
            compare(page.currentDisplayValue(page.schemaItems[0]), data.nextLabel)

            page = opened()
            keyClick(Qt.Key_Left)
            compare(appCore.values.shuffle, data.before)

            // In Camcorder's pane as in Standard.
            root.menuLayout = "Camcorder"
            root.framed = true
            page = opened()
            compare(menuRow(page, 0).value, data.label)
            keyClick(Qt.Key_Right)
            compare(appCore.values.shuffle, data.next)
            root.framed = false

            // In two steps: ▲ a choice on from the one it is on.
            root.menuLayout = "Broadcast"
            page = opened()
            compare(menuRow(page, 0).value, data.label)
            keyClick(Qt.Key_Return)
            compare(page.editIndex, 0)
            compare(menuRow(page, 0).value, data.label)
            keyClick(Qt.Key_Up)
            compare(menuRow(page, 0).value, data.nextLabel)
            compare(appCore.values.shuffle, data.saved)
            keyClick(Qt.Key_Return)
            compare(appCore.values.shuffle, data.next)
        }

        // A dynamic list on a choice its backend no longer offers (a server
        // gone), or on none: the row is on its first choice, shown, stepped
        // from and handed to the rows that go with it alike.
        function test_dynamicChoiceNotOffered() {
            appCore.schema = [
                { key: "server", label: "Server", type: "list_single", options_source: "dynamic",
                  options_slot: "serverOptions" },
                { key: "sync", label: "Sync", type: "toggle", default: "ON",
                  shown_with: { key: "server", value: "attic" } }
            ]
            appCore.values = { server: "cellar" }
            var page = open()
            // Before its choices are in, the row is what is saved.
            compare(page.valueOf(page.schemaItems[0]), "cellar")
            compare(page.currentDisplayValue(page.schemaItems[0]), "---")
            appCore.dynamicOptionsReady("com.osdos.test", "server",
                                        [{ id: "attic", label: "Attic" }, { id: "barn", label: "Barn" }])
            compare(page.currentDisplayValue(page.schemaItems[0]), "Attic")
            compare(page.valueOf(page.schemaItems[0]), "attic")
            compare(usable(page), ["server", "sync"])
            keyClick(Qt.Key_Right)
            compare(appCore.values.server, "barn")
            compare(usable(page), ["server"])
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

        // A manifest key in dot-notation is saved nested, as AppCore saves
        // it, and the page reads it back from there: the value saved shows,
        // a change is saved beside what else is under the same name and
        // shows at once, and a list of typed lines' page reads its lines.
        function test_dottedKey() {
            appCore.schema = [
                { key: "player.autoplay", label: "Autoplay", type: "toggle", default: "OFF" },
                { key: "player.name", label: "Name", type: "text", default: "" },
                { key: "lists.channels", label: "Channels", type: "text_list", max_length: 30 }
            ]
            appCore.values = { player: { autoplay: true, other: 1 }, lists: { channels: ["UCone", "UCtwo"] } }
            var page = open()
            compare(page.currentDisplayValue(page.schemaItems[0]), "ON")
            compare(page.currentDisplayValue(page.schemaItems[1]), "Not Set")
            compare(page.currentDisplayValue(page.schemaItems[2]), "2")
            keyClick(Qt.Key_Right)
            compare(appCore.values.player, { autoplay: false, other: 1 })
            compare(page.currentDisplayValue(page.schemaItems[0]), "OFF")
            appCore.put("com.osdos.test", "player.name", "Ada")
            loader.source = ""
            page = open()
            compare(page.currentDisplayValue(page.schemaItems[0]), "OFF")
            compare(page.currentDisplayValue(page.schemaItems[1]), "Ada")
            loader.source = ""
            loader.setSource(Qt.resolvedUrl("../../views/TextListSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "lists.channels",
                                            settingLabel: "Channels", maxLength: 30 } })
            verify(loader.item !== null, "loaded TextListSettings.qml")
            compare(loader.item.lines, ["UCone", "UCtwo"])
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
            // ADD, the last row, which ▼ comes to past the lines and ▲ round
            // from the first: a line typed, kept at the foot, the cursor left
            // on ADD.
            compare(lines.currentIndex, 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(lines.currentIndex, 2)
            verify(lines.onAdd)
            keyClick(Qt.Key_Down)
            compare(lines.currentIndex, 0)
            keyClick(Qt.Key_Up)
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
            keyClick(Qt.Key_Down)
            compare(lines.currentIndex, 0)
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
        // A row that works only with an app setting (Settings → Startup's
        // Start on Module naming the module, "shown_with": { "app", "value",
        // "reason" }): dimmed with its reason while that isn't so, usable
        // once it is, following a change made while the page is open.
        function test_shownWithAppSetting() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "auto_launch", label: "Auto-Launch Playback", type: "toggle", default: "OFF",
                  shown_with: { app: "startup_module", value: "com.osdos.test",
                                reason: "Only with Start on Module on Test (Settings > Startup)" } }
            ]
            var page = open()
            compare(off(page, "auto_launch"), "Only with Start on Module on Test (Settings > Startup)")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.values.auto_launch, undefined)
            var help = findChild(page, "helpLine")
            compare(help.text, "Only with Start on Module on Test (Settings > Startup)\n")

            appCore.appValues = { startup_module: "com.osdos.test" }
            appCore.appSettingChanged("startup_module", "com.osdos.test")
            compare(off(page, "auto_launch"), "")
            compare(cursor(page), "auto_launch")
            keyClick(Qt.Key_Return)
            compare(appCore.values.auto_launch, true)

            appCore.appSettingChanged("startup_module", "None")
            compare(off(page, "auto_launch"), "Only with Start on Module on Test (Settings > Startup)")
            loader.source = ""
            // Saved before the page opens.
            appCore.appValues = { startup_module: "com.osdos.test" }
            page = open()
            compare(off(page, "auto_launch"), "")
        }

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
            var line = root.menuRowHeight
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
            var line = root.menuRowHeight
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

        // A multiselect_submenu row shows how many of its choices are on:
        // All, None or "2 of 3", as its page has them (saved, else their
        // default); before its choices come, how many are saved off. Its
        // choices are asked for as the page opens, but not while a sign-in
        // dims it. Its page shows the row's help on a choice's line.
        function test_multiSelectValue() {
            appCore.schema = [
                { key: "picks", label: "Picks", type: "multiselect_submenu", options_source: "dynamic",
                  options_slot: "getPicks", description: "Which picks show" },
                { key: "libraries", label: "Libraries", type: "multiselect_submenu", options_source: "dynamic",
                  options_slot: "getLibraries", requires_auth: true }
            ]
            appCore.values = { picks: { a: false }, libraries: { x: false, y: false } }
            var page = open()
            compare(appCore.actions, ["getPicks"])
            compare(menuRow(page, 0).value, "1 Off")
            compare(menuRow(page, 1).value, "2 Off")
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [
                { label: "Group", heading: true }, { id: "a", label: "A" }, { id: "b", label: "B" },
                { id: "c", label: "C", default: false }])
            compare(menuRow(page, 0).value, "1 of 3")
            loader.source = ""
            appCore.values = { picks: { a: true, c: true } }
            page = open()
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [
                { id: "a", label: "A" }, { id: "b", label: "B" }, { id: "c", label: "C", default: false }])
            compare(menuRow(page, 0).value, "All")
            loader.source = ""
            appCore.values = { picks: { a: false, b: false } }
            page = open()
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [
                { id: "a", label: "A" }, { id: "b", label: "B" }, { id: "c", label: "C", default: false }])
            compare(menuRow(page, 0).value, "None")
            // A server that couldn't say: what is saved.
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [{ problem: "Out of reach" }])
            compare(menuRow(page, 0).value, "2 Off")

            keyClick(Qt.Key_Return)
            compare(root.navigations[0].params.help, "Which picks show")
            loader.setSource(Qt.resolvedUrl("../../views/MultiSelectSettings.qml"),
                             { navParams: root.navigations[0].params })
            var sub = loader.item
            sub.forceActiveFocus()
            var help = findChild(sub, "helpLine")
            compare(help.text, "", "Loading…")
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [
                { label: "Group", heading: true }, { id: "a", label: "A" }])
            compare(help.text, "Turns all of the choices under it on or off")
            keyClick(Qt.Key_Down)
            compare(help.text, "Which picks show")
            verify(help.visible)
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

        // An action row calls its slot, and the help line says what became
        // of it, in the row's own words ("done") or with its label, with
        // Settings' Help Line off too, until the cursor moves on; where the
        // backend has no such slot, that it wasn't done.
        function test_actionSaysWhatItDid() {
            appCore.schema = [
                { key: "rescan", label: "Rescan Scripts", type: "action", action_slot: "rescanScripts",
                  done: "Scripts folder read again", description: "Re-read the scripts folder" },
                { key: "tidy", label: "Tidy Up", type: "action", action_slot: "tidy" },
                { key: "video", label: "Video", type: "list_single", options: ["Auto", "OpenGL"], default: "Auto" }
            ]
            var page = open()
            var help = findChild(page, "helpLine")
            verify(help !== null)
            compare(help.text, "Re-read the scripts folder")
            root.helpLine = false
            verify(!help.shown)
            keyClick(Qt.Key_Return)
            compare(appCore.actions, ["rescanScripts"])
            compare(help.text, "Scripts folder read again")
            verify(help.visible)
            verify(help.shown, "said with Help Line off too")
            compare(help.opacity, 1)
            root.helpLine = true
            // No question was asked.
            verify(!findChild(page, "confirmPrompt").visible)

            keyClick(Qt.Key_Down)
            compare(cursor(page), "tidy")
            verify(!help.visible, "a row with nothing to say")
            keyClick(Qt.Key_Return)
            compare(appCore.actions, ["rescanScripts", "tidy"])
            compare(help.text, "Tidy Up: done")
            verify(help.visible)

            appCore.noSuchAction = true
            keyClick(Qt.Key_Return)
            compare(appCore.actions, ["rescanScripts", "tidy"])
            compare(help.text, "Not done: Tidy Up isn't offered by the module now")
            keyClick(Qt.Key_Up)
            compare(help.text, "Re-read the scripts folder")
        }

        // A row that deletes or signs out (its "confirm") asks first, in the
        // standard question window: the question, what the row does under
        // it, Cancel under the cursor and then the row's verb (its "verb",
        // else its label). Cancel and back do nothing and leave nothing
        // said; the verb calls the slot, and the help line says what became
        // of it.
        function test_actionAsksFirst() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "delete_history", label: "Delete Recently Watched", type: "action",
                  action_slot: "delete_history", confirm: "Delete Recently Watched?", verb: "Delete",
                  done: "Recently Watched deleted", description: "Empties Recently Watched" }
            ]
            var page = open()
            keyClick(Qt.Key_Down)
            var prompt = findChild(page, "confirmPrompt")
            var help = findChild(page, "helpLine")
            verify(prompt !== null && !prompt.visible)
            keyClick(Qt.Key_Return)
            verify(prompt.visible)
            compare(appCore.actions, [])
            compare(prompt.promptText, "Delete Recently Watched?")
            compare(prompt.promptKind, "question")
            compare(prompt.subtitleText, "Empties Recently Watched")
            compare(prompt.choices.map(function(c) { return c.label }), ["Cancel", "Delete"])
            compare(prompt.choices[prompt.choiceIndex].label, "Cancel")

            // Cancel.
            keyClick(Qt.Key_Return)
            verify(!prompt.visible)
            compare(appCore.actions, [])
            compare(help.text, "Empties Recently Watched")
            // Back.
            keyClick(Qt.Key_Return)
            verify(prompt.visible)
            compare(prompt.choices[prompt.choiceIndex].label, "Cancel")
            keyClick(Qt.Key_Escape)
            verify(!prompt.visible)
            compare(appCore.actions, [])
            compare(help.text, "Empties Recently Watched")
            // Delete.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            compare(prompt.choices[prompt.choiceIndex].label, "Delete")
            keyClick(Qt.Key_Return)
            verify(!prompt.visible)
            compare(appCore.actions, ["delete_history"])
            compare(help.text, "Recently Watched deleted")
            compare(cursor(page), "delete_history")
            // The list has the keys again.
            keyClick(Qt.Key_Up)
            compare(cursor(page), "enabled")
            verify(!help.visible)
        }

        // A sign-out row dimmed while no one is signed in asks nothing and
        // does nothing. Signed in, it asks; signed out by it, the row is
        // dimmed again where it stands, and what it did is still said.
        function test_signOutAsksFirst() {
            appCore.schema = [
                { key: "logout", label: "Sign Out", type: "action", action_slot: "logout",
                  confirm: "Sign out of Plex?", done: "Signing out of Plex", requires_auth: true }
            ]
            var page = open()
            var prompt = findChild(page, "confirmPrompt")
            var help = findChild(page, "helpLine")
            compare(off(page, "logout"), "Only once signed in")
            keyClick(Qt.Key_Return)
            verify(!prompt.visible)
            compare(appCore.actions, [])

            appCore.authState = "user"
            appCore.moduleAuthStateChanged("com.osdos.test")
            compare(off(page, "logout"), "")
            keyClick(Qt.Key_Return)
            verify(prompt.visible)
            compare(prompt.promptText, "Sign out of Plex?")
            compare(prompt.choices.map(function(c) { return c.label }), ["Cancel", "Sign Out"])
            compare(prompt.choices[prompt.choiceIndex].label, "Cancel")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.actions, ["logout"])
            compare(help.text, "Signing out of Plex")
            appCore.authState = ""
            appCore.moduleAuthStateChanged("com.osdos.test")
            compare(off(page, "logout"), "Only once signed in")
            compare(help.text, "Signing out of Plex")
            verify(menuRow(page, 0).dimmed)
        }

        // The hint bar names the keys that do something on the row under
        // the cursor: ◄ ► on a value, select on a line that opens, the
        // action's own verb on an action, nothing but the way on and back
        // on a dimmed row; select starts a change in the two-step layouts.
        // The multi-select and text-list pages word theirs by their line.
        function test_hintsByRow_data() {
            return [{ tag: "Standard", layout: "Standard" }, { tag: "Broadcast", layout: "Broadcast" }]
        }
        function test_hintsByRow(data) {
            appCore.schema = [
                { key: "loop", label: "Loop", type: "toggle", default: "OFF" },
                { key: "mode", label: "Mode", type: "list_single", options: ["A", "B"], default: "A" },
                { key: "more", label: "More", type: "submenu", settings: [] },
                { key: "picks", label: "Picks", type: "multiselect_submenu", options_source: "dynamic",
                  options_slot: "getPicks" },
                { key: "extra", label: "Extra", type: "toggle", default: "OFF",
                  shown_with: { key: "loop", value: "ON" } },
                { key: "wipe", label: "Delete History", type: "action", action_slot: "wipe",
                  confirm: "Delete History?", verb: "Delete" },
                { key: "logout", label: "Sign Out", type: "action", action_slot: "logout", confirm: "Sign out?" }
            ]
            root.menuLayout = data.layout
            var page = open()
            var bar = findChild(page, "hintBar")
            var two = data.layout === "Broadcast"
            var change = two ? "[ENTER]:CHANGE" : "[◄►]:CHANGE"
            var expected = ["[ESC]:BACK [▲▼]:NAVIGATE " + change, "[ESC]:BACK [▲▼]:NAVIGATE " + change,
                            "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:OPEN", "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:OPEN",
                            "[ESC]:BACK [▲▼]:NAVIGATE", "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:DELETE",
                            "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:SIGN OUT"]
            for (var i = 0; i < expected.length; i++) {
                compare(bar.text, expected[i], page.schemaItems[i].key)
                keyClick(Qt.Key_Down)
            }
            // Round to Loop. The row usable once Loop is on: its hint
            // follows in place.
            compare(cursor(page), "loop")
            if (two) {
                keyClick(Qt.Key_Return)
                compare(bar.text, "[ESC]:CANCEL [▲▼]:CHANGE [ENTER]:CONFIRM")
                keyClick(Qt.Key_Up)
                keyClick(Qt.Key_Return)
            } else {
                keyClick(Qt.Key_Right)
            }
            compare(appCore.values.loop, true)
            for (i = 0; i < 4; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "extra")
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE " + change)
        }
        function test_hintsOnSubPages() {
            appCore.schema = [{ key: "picks", label: "Picks", type: "multiselect_submenu",
                                options_source: "dynamic", options_slot: "getPicks" }]
            loader.setSource(Qt.resolvedUrl("../../views/MultiSelectSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "picks", settingLabel: "Picks" } })
            var page = loader.item
            page.forceActiveFocus()
            var bar = findChild(page, "hintBar")
            compare(bar.text, "[ESC]:BACK", "Loading…")
            appCore.dynamicOptionsReady("com.osdos.test", "picks", [
                { label: "Genres", heading: true }, { id: "a", label: "A" }])
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE [◄►]/[ENTER]:CHANGE ALL")
            keyClick(Qt.Key_Down)
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE [◄►]/[ENTER]:CHANGE")
            loader.source = ""

            appCore.values = { lines: ["one", "two"] }
            loader.setSource(Qt.resolvedUrl("../../views/TextListSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "lines", settingLabel: "Lines",
                                            maxLines: 2 } })
            page = loader.item
            page.forceActiveFocus()
            bar = findChild(page, "hintBar")
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:CHANGE")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE", "ADD, the list full")
            page.maxLines = 3
            compare(bar.text, "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:ADD")
        }

        // A dynamic list never saved shows the choice its backend marks as
        // where an install starts (Catalog Region: the time zone's country),
        // else the row's default by its id, and ◄ ► step from it; one
        // saved keeps what it saved. The names show, the ids are saved.
        function test_dynamicDefaultChoice() {
            appCore.schema = [
                { key: "region", label: "Catalog Region", type: "list_single", options_source: "dynamic",
                  options_slot: "get_regions", default: "US" },
                { key: "lang", label: "Language", type: "list_single", options_source: "dynamic",
                  options_slot: "get_langs", default: "-" }
            ]
            var regions = [{ id: "DE", label: "Germany" }, { id: "TR", label: "Turkey", default: true },
                           { id: "US", label: "USA" }]
            var page = open()
            appCore.dynamicOptionsReady("com.osdos.test", "region", regions)
            appCore.dynamicOptionsReady("com.osdos.test", "lang", [{ id: "en", label: "English" }, { id: "-", label: "Any" }])
            compare(menuRow(page, 0).value, "Turkey")
            compare(menuRow(page, 1).value, "Any")
            keyClick(Qt.Key_Right)
            compare(appCore.values.region, "US")
            compare(menuRow(page, 0).value, "USA")
            loader.source = ""
            appCore.values = { region: "DE" }
            page = open()
            appCore.dynamicOptionsReady("com.osdos.test", "region", regions)
            compare(menuRow(page, 0).value, "Germany")
        }

        // Subtitles in every module's order, Off, Forced Only, On, with
        // Forced Only where nothing is saved (or something no choice is):
        // what was saved, an old true or false too, keeps its meaning.
        function test_subtitlesOrder_data() {
            return [{ tag: "unset", saved: undefined, shown: "Forced Only", left: "off" },
                    { tag: "old on", saved: true, shown: "On", left: "forced" },
                    { tag: "old off", saved: false, shown: "Forced Only", left: "off" },
                    { tag: "off", saved: "off", shown: "Off", left: "on" },
                    { tag: "unknown", saved: "sometimes", shown: "Forced Only", left: "off" }]
        }
        function test_subtitlesOrder(data) {
            appCore.schema = [{ key: "auto_subtitles", label: "Subtitles", type: "list_single",
                                options_source: "dynamic", options_slot: "get_auto_subtitles_options",
                                default: "forced" }]
            appCore.values = data.saved === undefined ? {} : { auto_subtitles: data.saved }
            var page = open()
            appCore.dynamicOptionsReady("com.osdos.test", "auto_subtitles", [
                { id: "off", label: "Off" }, { id: "forced", label: "Forced Only", old: false, default: true },
                { id: "on", label: "On", old: true }])
            compare(menuRow(page, 0).value, data.shown)
            keyClick(Qt.Key_Left)
            compare(appCore.values.auto_subtitles, data.left)
        }

        // A list's own choices may each be { "id", "label" }: the label
        // shown over the id saved (Weather's Time per Screen, "1 min" over
        // "60 SEC"), ◄ ► and the two-step layouts stepping by them, a row
        // shown with it following the id; a value no choice is shows as
        // saved.
        function test_staticChoiceLabels_data() {
            return [{ tag: "Standard", layout: "Standard" }, { tag: "Flat Panel", layout: "Flat Panel" }]
        }
        function test_staticChoiceLabels(data) {
            appCore.schema = [
                { key: "screen_time", label: "Time per Screen", type: "list_single", default: "10 SEC",
                  options: [{ id: "10 SEC", label: "10 sec" }, { id: "30 SEC", label: "30 sec" },
                            { id: "60 SEC", label: "1 min" }] },
                { key: "fast", label: "Fast", type: "toggle", default: "OFF",
                  shown_with: { key: "screen_time", value: "60 SEC" } }
            ]
            root.menuLayout = data.layout
            var page = open()
            compare(menuRow(page, 0).value, "10 sec")
            verify(off(page, "fast") !== "")
            if (root.twoStep) {
                keyClick(Qt.Key_Return)
                keyClick(Qt.Key_Down)
                compare(menuRow(page, 0).value, "1 min")
                keyClick(Qt.Key_Return)
            } else {
                keyClick(Qt.Key_Left)
            }
            compare(appCore.values.screen_time, "60 SEC")
            compare(menuRow(page, 0).value, "1 min")
            compare(off(page, "fast"), "")
            loader.source = ""
            appCore.values = { screen_time: "15 SEC" }
            page = open()
            compare(menuRow(page, 0).value, "15 SEC")
        }

        // A row moved out of a submenu onto the first page (YouTube's
        // Subtitles) reads what was saved while it was in there: a
        // submenu's rows are saved flat under the module, by their keys.
        function test_rowMovedOutOfSubmenu() {
            appCore.values = { subtitles: "On", playback_speed: "2x" }
            appCore.schema = [
                { key: "advanced", label: "Advanced", type: "submenu", settings: [
                    { key: "subtitles", label: "Subtitles", type: "list_single", options: ["Off", "On"], default: "Off" },
                    { key: "playback_speed", label: "Playback Speed", type: "list_single", options: ["1x", "2x"], default: "1x" }] }
            ]
            loader.setSource(Qt.resolvedUrl("../../views/ModuleSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", submenu: "advanced" } })
            compare(menuRow(loader.item, 0).value, "On")
            loader.source = ""
            appCore.schema = [
                { key: "subtitles", label: "Subtitles", type: "list_single", options: ["Off", "On"], default: "Off" },
                { key: "advanced", label: "Advanced", type: "submenu", settings: [
                    { key: "playback_speed", label: "Playback Speed", type: "list_single", options: ["1x", "2x"], default: "1x" }] }
            ]
            var page = open()
            compare(menuRow(page, 0).value, "On")
            keyClick(Qt.Key_Right)
            compare(appCore.values.subtitles, "Off")
            loader.source = ""
            loader.setSource(Qt.resolvedUrl("../../views/ModuleSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", submenu: "advanced" } })
            compare(menuRow(loader.item, 0).value, "2x")
        }

        // An account module's two rows: SIGN IN after ENABLED, its value who
        // is signed in (the name its slot sends) or SIGNED OUT, opening the
        // module's own sign-in; SIGN OUT last, dimmed while signed out. The
        // rows that need an account say where to sign in.
        function test_signInRow() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "sign_in", label: "Sign In", type: "module_view", account: true,
                  options_source: "dynamic", options_slot: "get_account", params: { signIn: true } },
                { key: "libraries", label: "Libraries", type: "multiselect_submenu", options_source: "dynamic",
                  options_slot: "getLibraries", requires_auth: true },
                { key: "logout", label: "Sign Out", type: "action", action_slot: "logout",
                  confirm: "Sign out?", requires_auth: true }
            ]
            appCore.authState = "none"
            var page = open()
            compare(appCore.actions, ["get_account"])
            compare(menuRow(page, 1).value, "Signed Out")
            verify(menuRow(page, 1).opens)
            verify(!menuRow(page, 1).dimmed)
            compare(off(page, "libraries"), "Sign in first (SIGN IN above)")
            compare(off(page, "logout"), "Sign in first (SIGN IN above)")
            verify(menuRow(page, 3).dimmed)

            appCore.authState = "authed"
            appCore.moduleAuthStateChanged("com.osdos.test")
            compare(appCore.actions.filter(function(a) { return a === "get_account" }).length, 2)
            compare(menuRow(page, 1).value, "Signed In")
            appCore.dynamicOptionsReady("com.osdos.test", "sign_in", [{ id: "Ann", label: "Ann" }])
            compare(menuRow(page, 1).value, "Ann")
            compare(off(page, "libraries"), "")
            compare(off(page, "logout"), "")

            appCore.authState = "none"
            appCore.moduleAuthStateChanged("com.osdos.test")
            compare(menuRow(page, 1).value, "Signed Out")

            keyClick(Qt.Key_Down)
            compare(cursor(page), "sign_in")
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "modules/com.osdos.test/views/Root.qml")
            compare(root.navigations[0].params, { signIn: true })
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

        // In a monitor layout's frame (Settings → Look → Menu Layout) a
        // slider is its line alone, its value a number. Studio Monitor
        // changes a value in place with ◄ ►. Broadcast and Flat Panel change
        // one in two steps, as Settings does there: select starts it, ▲ ▼
        // step it with nothing saved and the cursor held, back leaves it,
        // select saves it; ► changes nothing.
        function test_monitorLayouts_data() {
            return [{ tag: "Studio Monitor", layout: "Studio Monitor", twoStep: false },
                    { tag: "Broadcast", layout: "Broadcast", twoStep: true },
                    { tag: "Flat Panel", layout: "Flat Panel", twoStep: true }]
        }
        function test_monitorLayouts(data) {
            appCore.schema = [{ key: "mode", label: "Mode", type: "list_single", options: ["A", "B", "C"], default: "A" },
                              { key: "loud", label: "Loudness", type: "slider", default: 50, step: 10 },
                              { key: "on", label: "Loop", type: "toggle", default: "OFF" }]
            root.menuLayout = data.layout
            var page = open()
            var list = menuOf(page)
            compare(list.x, root.chrome.listX)
            compare(list.y, root.chrome.listY)
            compare(list.height, root.chrome.listHeight)
            compare(root.chrome.title, data.layout === "Flat Panel" ? ">Settings >Test" : "Test")
            // The slider: one line, "50%".
            compare(menuRow(page, 1).value, "50%")
            compare(menuRow(page, 1).parent.height, menuRow(page, 0).height)
            compare(root.twoStep, data.twoStep)
            if (!data.twoStep) {
                keyClick(Qt.Key_Right)
                compare(appCore.values.mode, "B")
                compare(page.editIndex, -1)
                keyClick(Qt.Key_Down)
                keyClick(Qt.Key_Right)
                compare(menuRow(page, 1).value, "60%")
                return
            }
            keyClick(Qt.Key_Right)
            compare(appCore.values.mode, undefined)
            keyClick(Qt.Key_Return)
            compare(page.editIndex, 0)
            keyClick(Qt.Key_Up)
            compare(cursorIndex(page), 0, "the cursor stays on the row")
            compare(menuRow(page, 0).value, "B")
            verify(menuRow(page, 0).editing)
            compare(appCore.values.mode, undefined)
            keyClick(Qt.Key_Escape)
            compare(page.editIndex, -1)
            compare(menuRow(page, 0).value, "A")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            compare(menuRow(page, 0).value, "C")
            keyClick(Qt.Key_Return)
            compare(page.editIndex, -1)
            compare(appCore.values.mode, "C")
            // The slider, two steps up; the toggle, one.
            keyClick(Qt.Key_Down)
            compare(cursorIndex(page), 1)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(menuRow(page, 1).value, "70%")
            compare(appCore.values.loud, undefined)
            keyClick(Qt.Key_Return)
            compare(appCore.values.loud, 70)
            // No further than an end: the steps past it aren't counted, so
            // the first step back moves the value.
            keyClick(Qt.Key_Return)
            for (var i = 0; i < 5; i++)
                keyClick(Qt.Key_Up)
            compare(menuRow(page, 1).value, "100%")
            keyClick(Qt.Key_Down)
            compare(menuRow(page, 1).value, "90%")
            for (i = 0; i < 12; i++)
                keyClick(Qt.Key_Down)
            compare(menuRow(page, 1).value, "0%")
            keyClick(Qt.Key_Up)
            compare(menuRow(page, 1).value, "10%")
            compare(appCore.values.loud, 70)
            keyClick(Qt.Key_Return)
            compare(appCore.values.loud, 10)
            compare(menuRow(page, 1).value, "10%")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            compare(menuRow(page, 2).value, "ON")
            keyClick(Qt.Key_Return)
            compare(appCore.values.on, true)
        }
    }
}
