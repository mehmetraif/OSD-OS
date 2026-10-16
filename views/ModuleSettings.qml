import QtQuick
import Components

FocusScope {
    id: moduleSettingsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""
    property string moduleName: ""
    // A submenu row (manifest "type": "submenu", its own rows in "settings")
    // opens this view again on just those rows, e.g. YouTube's ADVANCED.
    property string submenuKey: navParams.submenu || ""
    property string submenuLabel: ""

    property var schemaItems: []    // filtered schema entries from JSON
    property var currentValues: ({}) // config.modules[moduleId]
    property var dynamicOptions: ({}) // key -> [{id, label}] loaded from backend
    property string authState: ""

    function loadSettings() {
        var allSettings = appCore.get_settings()
        currentValues = (allSettings.modules && allSettings.modules[moduleId]) ? allSettings.modules[moduleId] : {}

        var mods = appCore.get_installed_modules()
        for (var i = 0; i < mods.length; i++) {
            if (mods[i].id === moduleId) { moduleName = mods[i].name; break }
        }

        buildModel()
    }

    // The submenu row with this key, at any depth.
    function findSubmenu(list, key) {
        var row = findRow(list, key)
        return row && row.type === "submenu" ? row : null
    }

    // `keepKey`: the row the cursor stays on, as rows come and go round it.
    function buildModel(keepKey) {
        var schema = appCore.get_module_settings_schema(moduleId)
        fullSchema = schema
        if (submenuKey !== "") {
            var submenu = findSubmenu(schema, submenuKey)
            submenuLabel = submenu ? (submenu.label || "") : ""
            schema = submenu ? (submenu.settings || []) : []
        }
        menuSchema = schema
        var needsAuthState = false
        for (var n = 0; n < schema.length; n++) {
            if (schema[n].requires_auth) {
                needsAuthState = true
                break
            }
        }
        authState = needsAuthState ? appCore.get_module_auth_state(moduleId) : ""
        var caps = dynamicOptions["_capabilities"] || []
        var filtered = []
        for (var i = 0; i < schema.length; i++) {
            var item = schema[i]
            if (item.requires_auth && (authState === "none" || authState === "")) continue
            if (item.requires_capability && caps.indexOf(item.requires_capability) < 0) continue
            if (item.shown_with && !shownWith(item.shown_with)) continue
            filtered.push(item)
        }
        schemaItems = filtered

        if (filtered.length > 0) {
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            for (var k = 0; keepKey !== undefined && k < filtered.length; k++) {
                if (filtered[k].key === keepKey) restore = k
            }
            settingsList.currentIndex = Math.min(restore, filtered.length - 1)
            settingsList.show(settingsList.currentIndex)
        }
    }

    // The module's whole schema, submenus and all, and this menu's rows, as
    // buildModel() last read them.
    property var fullSchema: []
    property var menuSchema: []

    // A row offered only while another of the module's has a value
    // (manifest "shown_with": { "key", "value" }), such as the Arcade
    // module's rows for its Arcade screen: that row's value as the page
    // shows it, and never while that row is hidden itself.
    function shownWith(cond, depth) {
        var row = findRow(fullSchema, cond.key)
        depth = depth || 0
        if (!row || depth > 8)
            return false
        if (row.shown_with && !shownWith(row.shown_with, depth + 1))
            return false
        return String(valueOf(row)) === String(cond.value)
    }

    // A row's value as the page shows it: a toggle's "ON" or "OFF", a list's
    // choice, a slider's 0 to 100, an unset one's default (a dynamic list's
    // first choice).
    function valueOf(item) {
        var saved = currentValues[item.key]
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
            var opts = dynamicOptions[item.key] || []
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

    // Whether a row of this menu comes and goes with this key's value.
    function rowsFollow(key) {
        for (var i = 0; i < menuSchema.length; i++) {
            if (menuSchema[i].shown_with && menuSchema[i].shown_with.key === key) return true
        }
        return false
    }

    // Asks the backend for the choices of the dynamic rows shown that it
    // hasn't sent yet (a row just offered), or of all of them again (a
    // sign-in changes them). A multi-select row's are its own page's, which
    // asks for them as it opens.
    function requestOptions(again) {
        for (var i = 0; i < schemaItems.length; i++) {
            var item = schemaItems[i]
            if (item.options_source === "dynamic" && item.options_slot && item.type !== "multiselect_submenu"
                && (again || dynamicOptions[item.key] === undefined))
                appCore.invoke_module_action(moduleId, item.options_slot)
        }
    }

    // The key of the row the cursor is on.
    function currentKey() {
        var item = schemaItems[settingsList.currentIndex]
        return item ? item.key : undefined
    }

    // Rows that show their current value after the dots.
    function hasValue(item) {
        return item.type === "toggle" || item.type === "list_single" || item.type === "directory_browser"
    }

    function currentDisplayValue(item) {
        var key = item.key
        var type = item.type

        if (type === "toggle")
            return valueOf(item)

        if (type === "list_single") {
            if (item.options_source === "dynamic") {
                var opts = dynamicOptions[key] || []
                var storedId = currentValues[key] || null
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId || (storedId !== null && opts[i].old === storedId)) return opts[i].label
                }
                return opts.length > 0 ? opts[0].label : "---"
            }
            return valueOf(item) || "---"
        }

        if (type === "directory_browser") {
            var saved = currentValues[key] || ""
            return saved !== "" ? saved : "Default"
        }

        return ""
    }

    // A row's new value, shown and saved.
    function store(key, value) {
        var updated = Object.assign({}, currentValues)
        updated[key] = value
        currentValues = updated
        appCore.save_setting(moduleId, key, value)
    }

    function cycleValue(index, direction) {
        var item = schemaItems[index]
        if (!item) return

        if (item.type === "toggle") {
            store(item.key, currentDisplayValue(item) !== "ON")
            settingsList.forceLayout()
        } else if (item.type === "list_single") {
            var opts
            if (item.options_source === "dynamic") {
                opts = dynamicOptions[item.key] || []
                if (opts.length === 0) return
                var storedId = currentValues[item.key] || null
                var curIdx = 0
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId) { curIdx = i; break }
                }
                var newIdx = (curIdx + direction + opts.length) % opts.length
                store(item.key, opts[newIdx].id)
                if (item.apply_slot) {
                    appCore.invoke_module_action(moduleId, item.apply_slot)
                }
            } else {
                opts = item.options || []
                if (opts.length === 0) return
                // From the value shown: an unset one shows its default.
                var curVal = valueOf(item) || opts[0]
                var ci = opts.indexOf(curVal)
                if (ci < 0) ci = 0
                store(item.key, opts[(ci + direction + opts.length) % opts.length])
            }
            settingsList.forceLayout()
        } else if (item.type === "slider") {
            // A step toward one end, 10 unless the row says.
            var at = valueOf(item)
            var v = Math.max(0, Math.min(100, at + direction * (item.step || 10)))
            if (v === at) return
            store(item.key, v)
        }
        if (rowsFollow(item.key)) {
            buildModel(item.key)
            requestOptions(false)
        }
    }

    // Forward dynamicOptionsReady from appCore into our local cache
    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            if (mid !== moduleSettingsRoot.moduleId) return
            var updated = Object.assign({}, moduleSettingsRoot.dynamicOptions)
            updated[key] = items
            moduleSettingsRoot.dynamicOptions = updated
            // Capability changes affect schema filtering — rebuild the model so
            // settings with requires_capability appear/disappear immediately.
            if (key === "_capabilities")
                buildModel()
            else if (rowsFollow(key))
                buildModel(currentKey())
            else
                settingsList.forceLayout()
        }
        function onModuleAuthStateChanged(mid) {
            if (mid !== moduleSettingsRoot.moduleId) return
            buildModel()
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

        Keys.onLeftPressed: {
            moduleSettingsRoot.cycleValue(currentIndex, -1)
        }
        Keys.onRightPressed: {
            moduleSettingsRoot.cycleValue(currentIndex, 1)
        }

        Keys.onReturnPressed: {
            var item = schemaItems[currentIndex]
            if (!item) return
            if (item.type === "toggle") {
                moduleSettingsRoot.cycleValue(currentIndex, 1)
            } else if (item.type === "multiselect_submenu") {
                moduleSettingsRoot.navigateTo("views/MultiSelectSettings.qml", {
                    moduleId: moduleSettingsRoot.moduleId,
                    settingKey: item.key,
                    settingLabel: item.label
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "submenu") {
                moduleSettingsRoot.navigateTo("views/ModuleSettings.qml", {
                    moduleId: moduleSettingsRoot.moduleId,
                    submenu: item.key
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "module_view") {
                // The module itself, which its Root.qml opens on one of its
                // own views by these params, e.g. Netflix's sign-in page; back
                // from it comes back here. Enabled or not.
                moduleSettingsRoot.navigateTo(appCore.module_entry_point(moduleSettingsRoot.moduleId),
                                              item.params || {},
                                              { currentIndex: settingsList.currentIndex })
            } else if (item.type === "action") {
                appCore.invoke_module_action(moduleSettingsRoot.moduleId, item.action_slot)
            } else if (item.type === "directory_browser") {
                // Picked on the file picker's tree; "" is the module's own.
                moduleSettingsRoot.navigateTo("views/FilePicker.qml", {
                    moduleId: moduleSettingsRoot.moduleId,
                    settingKey: item.key,
                    currentPath: currentValues[item.key] || "",
                    defaultLabel: "Default Folder",
                    label: item.label
                }, { currentIndex: settingsList.currentIndex })
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                moduleSettingsRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "DISPLAY······ON". A slider
        // ("type": "slider") is its label, and the deck's tape bar in whole
        // lines under it, as in Settings: ◄ ► move the ▼ between its ends
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
                label: rowItem.modelData.label || ""
                value: moduleSettingsRoot.hasValue(rowItem.modelData)
                    ? moduleSettingsRoot.currentDisplayValue(rowItem.modelData) : ""
                selected: settingsList.currentIndex === rowItem.index
            }
            Loader {
                id: tape
                active: rowItem.slider
                x: root.sw * 0.009375 //6
                y: rowItem.lineHeight + Math.round((rowItem.barLines * rowItem.lineHeight - height) / 2)
                width: parent.width - 2 * x
                sourceComponent: OsdTapeBar {
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
    HelpLine {
        property var currentRow: moduleSettingsRoot.schemaItems[settingsList.currentIndex]
        visible: !!(currentRow && currentRow.description)
        text: (currentRow && currentRow.description) || ""
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
}
