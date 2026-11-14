import QtQuick
import Components

FocusScope {
    id: moduleSettingsRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    property string moduleId: navParams.moduleId || ""
    property string moduleName: ""
    // A submenu row (manifest "type": "submenu", its own rows in "settings")
    // opens this view again on just those rows, e.g. YouTube's ADVANCED.
    property string submenuKey: navParams.submenu || ""
    property string submenuLabel: ""
    // A group's page (Settings → Arcade): after this module's rows, those of
    // the group's other modules, each module under its name as a heading. A
    // row of another module is marked as its (_module): saved there, and its
    // choices, sign-in, pages and actions are that module's.
    property var alsoModules: submenuKey === "" ? (navParams.alsoModules || []) : []

    property var schemaItems: []    // filtered schema entries from JSON
    property var currentValues: ({}) // config.modules[moduleId]
    property var alsoValues: ({})    // module id -> config.modules[id], for alsoModules
    // key -> [{id, label}] loaded from backend; another module's as "id/key"
    // (rowId()).
    property var dynamicOptions: ({})
    property string authState: ""
    property var alsoAuthStates: ({}) // module id -> its sign-in state
    property var moduleNames: ({})    // module id -> its name

    function loadSettings() {
        var allSettings = appCore.get_settings()
        var saved = allSettings.modules || {}
        currentValues = saved[moduleId] || {}
        var also = {}
        for (var a = 0; a < alsoModules.length; a++)
            also[alsoModules[a]] = saved[alsoModules[a]] || {}
        alsoValues = also

        var mods = appCore.get_installed_modules()
        var names = {}
        for (var i = 0; i < mods.length; i++)
            names[mods[i].id] = mods[i].name
        moduleNames = names
        moduleName = names[moduleId] || ""

        buildModel()
    }

    // The module a row is of, and its name among the page's rows: its key,
    // another module's "id/key", which is also where its choices are kept.
    function moduleOf(item) {
        return item._module || moduleId
    }
    function rowId(item) {
        return optionKey(item._module || "", item.key)
    }
    function optionKey(mid, key) {
        return mid !== "" ? mid + "/" + key : key
    }
    // The values saved for a row's module.
    function valuesOf(item) {
        return item._module ? (alsoValues[item._module] || {}) : currentValues
    }

    // The submenu row with this key, at any depth.
    function findSubmenu(list, key) {
        var row = findRow(list, key)
        return row && row.type === "submenu" ? row : null
    }

    // `keepId`: the row the cursor stays on (rowId()), as the rows are made
    // again round it.
    function buildModel(keepId) {
        var schema = appCore.get_module_settings_schema(moduleId)
        fullSchema = schema
        if (submenuKey !== "") {
            var submenu = findSubmenu(schema, submenuKey)
            submenuLabel = submenu ? (submenu.label || "") : ""
            schema = submenu ? (submenu.settings || []) : []
        }
        menuSchema = schema
        authState = needsAuth(schema) ? appCore.get_module_auth_state(moduleId) : ""
        var schemas = {}
        var states = {}
        for (var a = 0; a < alsoModules.length; a++) {
            var mid = alsoModules[a]
            schemas[mid] = appCore.get_module_settings_schema(mid) || []
            states[mid] = needsAuth(schemas[mid]) ? appCore.get_module_auth_state(mid) : ""
        }
        alsoSchemas = schemas
        alsoAuthStates = states
        var filtered = offered(schema, "")
        for (a = 0; a < alsoModules.length; a++) {
            var rows = offered(schemas[alsoModules[a]], alsoModules[a])
            if (rows.length > 0)
                filtered = filtered.concat([{ type: "section", label: moduleNames[alsoModules[a]] || alsoModules[a] }], rows)
        }
        schemaItems = filtered

        if (filtered.length > 0) {
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            for (var k = 0; keepId !== undefined && k < filtered.length; k++) {
                if (filtered[k].type !== "section" && rowId(filtered[k]) === keepId) restore = k
            }
            restore = Math.min(restore, filtered.length - 1)
            // Never on a heading.
            while (restore < filtered.length - 1 && filtered[restore].type === "section")
                restore++
            settingsList.currentIndex = restore
            settingsList.show(settingsList.currentIndex)
        }
    }

    // Whether any of these rows wants to know who is signed in.
    function needsAuth(schema) {
        for (var n = 0; n < schema.length; n++) {
            if (schema[n].requires_auth)
                return true
        }
        return false
    }

    // A menu's rows on the page (another module's, `mid`, marked as its), all
    // of them: one a sign-in, a capability or another row's value keeps back
    // is dimmed rather than hidden, the cursor stopping on it and the help
    // line saying why (_off; _offBy, what keeps it back: "auth",
    // "capability" or "shown_with").
    function offered(schema, mid) {
        var state = mid !== "" ? (alsoAuthStates[mid] || "") : authState
        var caps = dynamicOptions[optionKey(mid, "_capabilities")] || []
        var rows = []
        for (var i = 0; i < schema.length; i++) {
            var item = schema[i]
            var off = "", by = ""
            if (item.requires_auth && (state === "none" || state === "")) {
                off = "Only once signed in"
                by = "auth"
            } else if (item.requires_capability && caps.indexOf(item.requires_capability) < 0) {
                off = "Not offered by the server"
                by = "capability"
            } else if (item.shown_with && !shownWith(item.shown_with, mid)) {
                off = shownWithReason(item.shown_with, mid)
                by = "shown_with"
            }
            var row = mid !== "" ? Object.assign({ _module: mid }, item) : item
            rows.push(off !== "" ? Object.assign({ _off: off, _offBy: by }, row) : row)
        }
        return rows
    }

    // What a row's "shown_with" waits for, in words: "Only with Screen on
    // Arcade", an on or off one's "Only with Online Play on", several "… or
    // …".
    function shownWithReason(cond, mid) {
        var parts = conditions(cond).map(function(one) {
            var row = findRow(mid !== "" ? (alsoSchemas[mid] || []) : fullSchema, one.key)
            var label = row ? (row.label || one.key) : one.key
            var value = String(one.value)
            var lower = value.toLowerCase()
            return lower === "on" || lower === "off" ? label + " " + lower : label + " on " + value
        })
        return "Only with " + parts.join(" or ")
    }

    // The module's whole schema, submenus and all, and this menu's rows, as
    // buildModel() last read them; the other modules' schemas by id.
    property var fullSchema: []
    property var menuSchema: []
    property var alsoSchemas: ({})

    // Whether a row usable only while another of the module's (`mid`, ""
    // for this page's own) has a value (manifest "shown_with": { "key",
    // "value" }), such as the Arcade module's rows for its Arcade screen, can
    // be used now: that row's value as the page shows it, and never while
    // that row can't be used itself. A list of them holds while any holds.
    function shownWith(cond, mid, depth) {
        if (cond.key === undefined)
            return conditions(cond).some(function(one) { return shownWith(one, mid, depth) })
        mid = mid || ""
        var row = findRow(mid !== "" ? (alsoSchemas[mid] || []) : fullSchema, cond.key)
        depth = depth || 0
        if (!row || depth > 8)
            return false
        if (row.shown_with && !shownWith(row.shown_with, mid, depth + 1))
            return false
        return String(valueOf(mid !== "" ? Object.assign({ _module: mid }, row) : row)) === String(cond.value)
    }

    // A row's "shown_with" as a list of its conditions: one, or a list of
    // them, which from the manifest is a sequence rather than a JS Array.
    function conditions(cond) {
        return !cond ? [] : cond.key !== undefined ? [cond] : Array.from(cond)
    }

    // A row's value as the page shows it: a toggle's "ON" or "OFF", a list's
    // choice, a slider's 0 to 100, an unset one's default (a dynamic list's
    // first choice).
    function valueOf(item) {
        var saved = valuesOf(item)[item.key]
        if (item.type === "slider") {
            var n = parseInt(saved)
            if (isNaN(n))
                n = item.default !== undefined ? item.default : 100
            return Math.max(0, Math.min(100, n))
        }
        if (item.type === "toggle") {
            if (saved === undefined || saved === null) saved = (item.default === "ON")
            return (saved === true || saved === "ON") ? "ON" : "OFF"
        }
        if (saved !== undefined && saved !== null && saved !== "")
            return saved
        if (item.options_source === "dynamic") {
            var opts = dynamicOptions[rowId(item)] || []
            return opts.length > 0 ? opts[0].id : ""
        }
        return item.default || ""
    }

    // The row with this key, at any depth.
    function findRow(list, key) {
        for (var i = 0; i < list.length; i++) {
            if (list[i].key === key) return list[i]
            var inner = list[i].type === "submenu" ? findRow(list[i].settings || [], key) : null
            if (inner) return inner
        }
        return null
    }

    // Whether a row of this menu is dimmed or not by this key's value, of
    // this page's own module or another's (`mid`).
    function rowsFollow(key, mid) {
        var rows = mid ? (alsoSchemas[mid] || []) : menuSchema
        for (var i = 0; i < rows.length; i++) {
            if (conditions(rows[i].shown_with).some(function(one) { return one.key === key })) return true
        }
        return false
    }

    // Asks the backend for the choices of the dynamic rows it hasn't been
    // asked for yet (a row a sign-in or a capability no longer keeps back),
    // or of all of them again (a sign-in changes them). A multi-select row's
    // are its own page's, which asks for them as it opens.
    property var askedOptions: ({})
    function requestOptions(again) {
        for (var i = 0; i < schemaItems.length; i++) {
            var item = schemaItems[i]
            var id = rowId(item)
            if (item.options_source === "dynamic" && item.options_slot && item.type !== "multiselect_submenu"
                && item._offBy !== "auth" && item._offBy !== "capability"
                && (again || (dynamicOptions[id] === undefined && !askedOptions[id]))) {
                askedOptions[id] = true
                appCore.invoke_module_action(moduleOf(item), item.options_slot)
            }
        }
    }

    // A backend that sends a row's choices again may have put its value back
    // (a profile switch that failed): the saved value is read again.
    function readAgain(mid, own, key) {
        var saved = ((appCore.get_settings().modules || {})[mid] || {})[key]
        var shown = own === "" ? currentValues : (alsoValues[own] || {})
        if (saved === undefined || shown[key] === saved) return
        var values = Object.assign({}, shown)
        values[key] = saved
        if (own === "") {
            currentValues = values
        } else {
            var all = Object.assign({}, alsoValues)
            all[own] = values
            alsoValues = all
        }
    }

    // The row the cursor is on (rowId()).
    function currentKey() {
        var item = schemaItems[settingsList.currentIndex]
        return item ? rowId(item) : undefined
    }

    // Rows that show their current value after the dots.
    function hasValue(item) {
        return item.type === "toggle" || item.type === "list_single" || item.type === "directory_browser"
               || item.type === "text" || item.type === "text_list"
    }

    function currentDisplayValue(item) {
        var key = item.key
        var type = item.type

        if (type === "toggle")
            return valueOf(item)

        if (type === "list_single") {
            if (item.options_source === "dynamic") {
                var opts = dynamicOptions[rowId(item)] || []
                var storedId = valuesOf(item)[key] || null
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId || (storedId !== null && opts[i].old === storedId)) return opts[i].label
                }
                return opts.length > 0 ? opts[0].label : "---"
            }
            return valueOf(item) || "---"
        }

        if (type === "directory_browser") {
            var saved = valuesOf(item)[key] || ""
            return saved !== "" ? saved : "Default"
        }

        // A line typed on the on-screen keyboard: what was typed, or that
        // something was (a key or a password, "secret", is never shown).
        if (type === "text") {
            var typed = String(valuesOf(item)[key] || "")
            return typed === "" ? "Not Set" : item.secret ? "Set" : typed
        }

        // A list of typed lines (its own page): how many there are.
        if (type === "text_list") {
            var lines = valuesOf(item)[key]
            var count = lines && lines.length !== undefined && typeof lines !== "string" ? lines.length : 0
            return count === 0 ? "None" : String(count)
        }

        return ""
    }

    // A row's new value, saved in its module and shown. One that couldn't be
    // saved (a full card) leaves the row on what is still saved: false.
    function store(item, value) {
        if (!appCore.save_setting(moduleOf(item), item.key, value))
            return false
        var updated = Object.assign({}, valuesOf(item))
        updated[item.key] = value
        if (item._module) {
            var all = Object.assign({}, alsoValues)
            all[item._module] = updated
            alsoValues = all
        } else {
            currentValues = updated
        }
        return true
    }

    function cycleValue(index, direction) {
        var item = schemaItems[index]
        if (!item || item._off) return

        if (item.type === "toggle") {
            store(item, currentDisplayValue(item) !== "ON")
            settingsList.forceLayout()
        } else if (item.type === "list_single") {
            var opts
            if (item.options_source === "dynamic") {
                opts = dynamicOptions[rowId(item)] || []
                if (opts.length === 0) return
                var storedId = valuesOf(item)[item.key] || null
                var curIdx = 0
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId) { curIdx = i; break }
                }
                var newIdx = (curIdx + direction + opts.length) % opts.length
                if (store(item, opts[newIdx].id) && item.apply_slot) {
                    appCore.invoke_module_action(moduleOf(item), item.apply_slot)
                }
            } else {
                opts = item.options || []
                if (opts.length === 0) return
                // From the value shown: an unset one shows its default.
                var curVal = valueOf(item) || opts[0]
                var ci = opts.indexOf(curVal)
                if (ci < 0) ci = 0
                store(item, opts[(ci + direction + opts.length) % opts.length])
            }
            settingsList.forceLayout()
        } else if (item.type === "slider") {
            // A step toward one end, 10 unless the row says.
            var at = valueOf(item)
            var v = Math.max(0, Math.min(100, at + direction * (item.step || 10)))
            if (v === at) return
            store(item, v)
        }
        if (rowsFollow(item.key, item._module)) {
            buildModel(rowId(item))
            requestOptions(false)
        }
    }

    // A line select leads on from, to a page of its own: a submenu's rows, a
    // multi-select's choices, the module's own view, the folder picker. ►
    // opens it too, and its line ends in a ►.
    // A dimmed line (_off) neither opens nor changes.
    function opens(item) {
        return !!item && !item._off && (item.type === "submenu" || item.type === "multiselect_submenu"
                                        || item.type === "module_view" || item.type === "directory_browser"
                                        || item.type === "text" || item.type === "text_list")
    }
    // A line whose value ◄ ► change on it: between a ◄ and a ►.
    function changes(item) {
        return !!item && !item._off && (item.type === "toggle" || item.type === "list_single")
    }

    // Forward dynamicOptionsReady from appCore into our local cache
    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            var own = mid === moduleSettingsRoot.moduleId ? "" : mid
            if (own !== "" && moduleSettingsRoot.alsoModules.indexOf(mid) < 0) return
            var updated = Object.assign({}, moduleSettingsRoot.dynamicOptions)
            updated[optionKey(own, key)] = items
            moduleSettingsRoot.dynamicOptions = updated
            readAgain(mid, own, key)
            // The rows made again, the cursor kept on its row, when the
            // choices decide which are offered: the capabilities
            // (requires_capability) or a row others follow (shown_with).
            if (key === "_capabilities" || rowsFollow(key, own))
                buildModel(currentKey())
            else
                settingsList.forceLayout()
        }
        function onModuleAuthStateChanged(mid) {
            if (mid !== moduleSettingsRoot.moduleId && moduleSettingsRoot.alsoModules.indexOf(mid) < 0) return
            buildModel(currentKey())
            requestOptions(true)
        }
    }

    Component.onCompleted: {
        loadSettings()
        // Kick off loading dynamic option lists for any dynamic settings
        requestOptions(true)
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: moduleSettingsRoot.submenuLabel !== ""
                  ? moduleName + " / " + moduleSettingsRoot.submenuLabel : moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: settingsList
        model: schemaItems
        focus: true

        // The page a line leads to (opens).
        function openRow(item) {
            if (item.type === "multiselect_submenu") {
                var mid = moduleSettingsRoot.moduleOf(item)
                moduleSettingsRoot.navigateTo("views/MultiSelectSettings.qml", {
                    moduleId: mid,
                    moduleName: mid === moduleSettingsRoot.moduleId ? moduleName : (moduleNames[mid] || mid),
                    settingKey: item.key,
                    settingLabel: item.label
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "submenu") {
                moduleSettingsRoot.navigateTo("views/ModuleSettings.qml", {
                    moduleId: moduleSettingsRoot.moduleOf(item),
                    submenu: item.key
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "module_view") {
                // The module itself, which its Root.qml opens on one of its
                // own views by these params, e.g. Netflix's sign-in page; back
                // from it comes back here. Enabled or not.
                moduleSettingsRoot.navigateTo(appCore.module_entry_point(moduleSettingsRoot.moduleOf(item)),
                                              item.params || {},
                                              { currentIndex: settingsList.currentIndex })
            } else if (item.type === "text_list") {
                // A list of typed lines, on a page of its own.
                var owner = moduleSettingsRoot.moduleOf(item)
                moduleSettingsRoot.navigateTo("views/TextListSettings.qml", {
                    moduleId: owner,
                    moduleName: owner === moduleSettingsRoot.moduleId ? moduleName : (moduleNames[owner] || owner),
                    settingKey: item.key,
                    settingLabel: item.label,
                    help: item.description || "",
                    applySlot: item.apply_slot || "",
                    maxLength: item.max_length || 64,
                    maxLines: item.max_lines || 50
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "text") {
                // Typed on the on-screen keyboard, over the page: what is
                // saved to start from, kept on OK, left as it was on back.
                moduleSettingsRoot.typingRow = item
                keyboard.title = item.label || ""
                keyboard.maxLength = item.max_length || 64
                keyboard.masked = !!item.secret
                keyboard.open(String(moduleSettingsRoot.valuesOf(item)[item.key] || ""))
            } else if (item.type === "directory_browser") {
                // Picked on the file picker's tree; "" is the module's own.
                moduleSettingsRoot.navigateTo("views/FilePicker.qml", {
                    moduleId: moduleSettingsRoot.moduleOf(item),
                    settingKey: item.key,
                    currentPath: moduleSettingsRoot.valuesOf(item)[item.key] || "",
                    defaultLabel: "Default Folder",
                    label: item.label
                }, { currentIndex: settingsList.currentIndex })
            }
        }

        Keys.onLeftPressed: {
            moduleSettingsRoot.cycleValue(currentIndex, -1)
        }
        Keys.onRightPressed: {
            var item = schemaItems[currentIndex]
            if (moduleSettingsRoot.opens(item))
                openRow(item)
            else
                moduleSettingsRoot.cycleValue(currentIndex, 1)
        }

        Keys.onReturnPressed: {
            var item = schemaItems[currentIndex]
            if (!item || item._off) return
            if (item.type === "toggle")
                moduleSettingsRoot.cycleValue(currentIndex, 1)
            else if (moduleSettingsRoot.opens(item))
                openRow(item)
            else if (item.type === "action")
                appCore.invoke_module_action(moduleSettingsRoot.moduleOf(item), item.action_slot)
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                moduleSettingsRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "DISPLAY······ON", another
        // module's under its name as a heading ("HIGH SCORES ─────"). A line
        // that leads to a page ends in a ►; a toggle's or a list's value,
        // which ◄ ► change, stands between a ◄ and a ►. A slider ("type":
        // "slider") is its label, and the deck's tape bar in whole lines
        // under it, as in Settings: ◄ ► move the ▼ between its ends
        // ("start_text", "end_text"), a "step" at a time.
        delegate: Item {
            id: rowItem
            required property var modelData
            required property int index
            readonly property bool slider: modelData.type === "slider"
            readonly property real lineHeight: root.sh * 0.0583333 //28
            readonly property int barLines: slider ? Math.ceil((tape.height + 2 * root.px) / lineHeight) : 0
            width: settingsList.width
            height: lineHeight * (1 + barLines)

            MenuRow {
                width: parent.width
                height: rowItem.lineHeight
                heading: rowItem.modelData.type === "section"
                label: rowItem.modelData.label || ""
                value: moduleSettingsRoot.hasValue(rowItem.modelData)
                    ? moduleSettingsRoot.currentDisplayValue(rowItem.modelData) : ""
                opens: moduleSettingsRoot.opens(rowItem.modelData)
                changes: moduleSettingsRoot.changes(rowItem.modelData)
                dimmed: !!rowItem.modelData._off
                selected: settingsList.currentIndex === rowItem.index
            }
            Loader {
                id: tape
                active: rowItem.slider
                visible: !rowItem.modelData._off
                x: root.sw * 0.009375 //6
                y: rowItem.lineHeight + Math.round((rowItem.barLines * rowItem.lineHeight - height) / 2)
                width: parent.width - 2 * x
                sourceComponent: OsdTapeBar {
                    changes: true
                    fontSize: root.sh * 0.0333333 //16
                    value: moduleSettingsRoot.valueOf(rowItem.modelData) / 100
                    startText: rowItem.modelData.start_text || ""
                    endText: rowItem.modelData.end_text || ""
                }
            }
        }
    }

    // --- HELP TEXT --- (shown when a focused row has a description), on one
    // line that scrolls when it is too long for the box.
    // A dimmed row's says first why it can't be used now.
    HelpLine {
        property var currentRow: moduleSettingsRoot.schemaItems[settingsList.currentIndex]
        visible: !!(currentRow && (currentRow.description || currentRow._off))
        text: (currentRow && currentRow._off ? currentRow._off + "\n" : "") + ((currentRow && currentRow.description) || "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A "text" row's keyboard: an address, a place's name, a key. Upper and
    // lower case as typed, and nothing typed is an answer too (the row unset).
    property var typingRow: null
    OnScreenKeyboard {
        id: keyboard
        objectName: "keyboard"
        anchors.fill: parent
        visible: false
        caseSensitive: true
        allowEmpty: true
        onAccepted: function(text) {
            var item = moduleSettingsRoot.typingRow
            moduleSettingsRoot.typingRow = null
            keyboard.close()
            // A key typed goes no further than the settings.
            keyboard.text = ""
            settingsList.forceActiveFocus()
            if (item && moduleSettingsRoot.store(item, text.trim()) && item.apply_slot)
                appCore.invoke_module_action(moduleSettingsRoot.moduleOf(item), item.apply_slot)
        }
        onCanceled: {
            moduleSettingsRoot.typingRow = null
            keyboard.close()
            keyboard.text = ""
            settingsList.forceActiveFocus()
        }
    }
}
