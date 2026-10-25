import QtQuick

// A video's own menu: back during playback opens it over the picture, which
// goes on playing (Transparent Background; MpvController's player menu, see
// noteSession's "menu"). Without Transparent Background the video ends for it
// and its player starts it again where it was as it closes. Its lines are the
// module's settings that matter while a video plays, taken from its manifest
// by key, wherever they sit (YouTube keeps its own in ADVANCED): ◄ ► change
// one and save it at once, as in the module's settings, and the host applies
// it to the video (settingChanged).
// Then the host's own lines (actions, acted on in activated), and last CLOSE
// VIDEO (activated("close")). Back takes the picture back to full screen
// (closed()). A key it has no use for goes on to its player, so play/pause
// still pauses the video under it.
//
//     PlayerMenu {
//         id: playerMenu
//         anchors.fill: parent
//         moduleId: moduleRoot.moduleId
//         iconSource: moduleRoot.moduleIcon
//         moduleName: moduleRoot.moduleName
//         keys: ["playback_speed", "video_scaling"]
//         onSettingChanged: function(key, value) { … }
//         onActivated: function(action) { … }
//         onClosed: …
//     }
//     … playerMenu.actions = [{ label: "Add to Favorites", action: "favorite" }]
//     … playerMenu.open(title)
FocusScope {
    id: menuRoot

    // A video's own menu: the theme's effects and menu music rest while it is
    // open, the playing as plain as it always is (Main.qml's root.effectsRest).
    readonly property bool resting: visible
    onRestingChanged: root.restFor(menuRoot, resting)
    Component.onDestruction: root.restFor(menuRoot, false)

    property string moduleId: ""
    property string iconSource: ""
    property string moduleName: ""
    // The settings it offers, by manifest key, in this order.
    property var keys: []
    // The host's own lines, after them: [{ label, action }].
    property var actions: []
    property string title: ""

    signal settingChanged(string key, var value)
    signal activated(string action)
    signal closed()

    visible: false
    // Hidden, it lets go of the keys: an item only hidden would keep them.
    enabled: visible

    property var rows: []
    property var values: ({})
    // key -> [{ id, label }], from the module's backend (options_slot).
    property var dynamicOptions: ({})

    function open(videoTitle) {
        title = videoTitle || ""
        build()
        list.currentIndex = 0
        visible = true
        list.forceActiveFocus()
    }

    function close() {
        visible = false
    }

    // The lines again, keeping the cursor: the host's change its words.
    function refresh() {
        var index = list.currentIndex
        build()
        list.currentIndex = Math.min(index, rows.length - 1)
    }

    // The setting with this key, at any depth of the manifest's settings.
    function findItem(items, key) {
        for (var i = 0; i < items.length; i++) {
            if (items[i].key === key)
                return items[i]
            var inner = findItem(items[i].settings || [], key)
            if (inner)
                return inner
        }
        return null
    }

    function build() {
        var schema = appCore ? appCore.get_module_settings_schema(moduleId) : []
        var found = []
        var current = {}
        for (var i = 0; i < keys.length; i++) {
            var item = findItem(schema, keys[i])
            if (!item || (item.type !== "list_single" && item.type !== "toggle"))
                continue
            found.push(item)
            current[item.key] = appCore.get_setting(moduleId, item.key)
            if (item.options_source === "dynamic" && item.options_slot && !dynamicOptions[item.key])
                appCore.invoke_module_action(moduleId, item.options_slot)
        }
        for (var a = 0; a < actions.length; a++)
            found.push({ type: "action", label: actions[a].label, action: actions[a].action })
        found.push({ type: "action", label: "Close Video", action: "close" })
        values = current
        rows = found
    }

    // What a setting's line shows after the dots, as the module's settings do.
    function displayValue(item) {
        var raw = values[item.key]
        if (item.type === "toggle") {
            if (raw === undefined || raw === null || raw === "")
                raw = (item.default === "ON")
            return (raw === true || raw === "ON") ? "ON" : "OFF"
        }
        if (item.type === "list_single") {
            if (item.options_source === "dynamic") {
                var opts = dynamicOptions[item.key] || []
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === raw || (raw !== undefined && raw !== null && opts[i].old === raw))
                        return opts[i].label
                }
                for (var d = 0; d < opts.length; d++) {
                    if (opts[d].id === item.default)
                        return opts[d].label
                }
                return opts.length > 0 ? opts[0].label : "---"
            }
            return raw || item.default || "---"
        }
        return ""
    }

    // One step of a setting toward -1 or 1, saved and handed to the host.
    function change(item, direction) {
        var value
        if (item.type === "toggle") {
            value = displayValue(item) !== "ON"
        } else if (item.options_source === "dynamic") {
            var opts = dynamicOptions[item.key] || []
            if (opts.length === 0)
                return
            var raw = values[item.key]
            if (raw === undefined || raw === null || raw === "")
                raw = item.default
            var at = 0
            for (var i = 0; i < opts.length; i++) {
                if (opts[i].id === raw || opts[i].old === raw) { at = i; break }
            }
            value = opts[(at + direction + opts.length) % opts.length].id
        } else {
            var list = item.options || []
            if (list.length === 0)
                return
            var ci = list.indexOf(values[item.key] || item.default || list[0])
            if (ci < 0)
                ci = 0
            value = list[(ci + direction + list.length) % list.length]
        }
        var updated = Object.assign({}, values)
        updated[item.key] = value
        values = updated
        appCore.save_setting(moduleId, item.key, value)
        if (item.apply_slot)
            appCore.invoke_module_action(moduleId, item.apply_slot)
        settingChanged(item.key, value)
    }

    // Select on a line: an action's, or a toggle's next value.
    function select(index) {
        var row = rows[index]
        if (!row)
            return
        if (row.type === "action")
            activated(row.action)
        else if (row.type === "toggle")
            change(row, 1)
    }

    // A Scaling (the module's video_scaling) on the video that plays, as
    // MpvController::sessionArgs() starts one with it: Default is Settings'.
    function applyScaling(value) {
        var s = (!value || value === "Default") ? (appCore.get_setting("", "video_scaling") || "Letterbox") : value
        mpvController.setVideoProperty("keepaspect", s !== "Anamorphic")
        mpvController.setVideoProperty("panscan", s === "Pan & Scan" ? 1.0 : (s === "14:9" ? 0.43 : 0.0))
    }

    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            if (mid !== menuRoot.moduleId)
                return
            var updated = Object.assign({}, menuRoot.dynamicOptions)
            updated[key] = items
            menuRoot.dynamicOptions = updated
        }
    }

    AppBar {
        iconSource: menuRoot.iconSource
        title: menuRoot.moduleName
        subtitle: menuRoot.title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The lines in the place every view's list has, ▲ / ▼ while some are
    // hidden, the cursor round the ends (MenuList's).
    MenuList {
        id: list
        model: menuRoot.rows
        focus: true

        Keys.onLeftPressed: {
            var row = menuRoot.rows[currentIndex]
            if (row && row.type !== "action")
                menuRoot.change(row, -1)
        }
        Keys.onRightPressed: {
            var row = menuRoot.rows[currentIndex]
            if (row && row.type !== "action")
                menuRoot.change(row, 1)
        }
        Keys.onReturnPressed: menuRoot.select(currentIndex)
        Keys.onEnterPressed: menuRoot.select(currentIndex)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                menuRoot.closed()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu, a setting's value between the ◄
        // and ► that change it: "SPEED······◄ 1X ►".
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: modelData.label || ""
            value: modelData.type === "action" ? "" : menuRoot.displayValue(modelData)
            changes: modelData.type !== "action"
            selected: list.currentIndex === index
        }
    }

    HelpLine {
        property var currentRow: menuRoot.rows[list.currentIndex]
        visible: !!(currentRow && currentRow.description)
        text: (currentRow && currentRow.description) || ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        text: root.hints.back + ":VIDEO " + root.hints.navigate + ":NAVIGATE "
              + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
