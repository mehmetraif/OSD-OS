import QtQuick
import Components

// A module's multiselect_submenu row (Plex's Libraries, Arcade's Menu
// Filters): a line for each of the backend's choices (dynamicOptionsReady,
// [{ id, label, default }]), ON or OFF, the row saved whole at every change
// as { "<id>": true or false } under its key, so an id may hold any
// character. A choice not saved there is as its default says, else ON. A
// heading among them ({ label, heading: true }) groups the choices under it,
// up to the next, and turns them all on at once, or all off once they all
// are on.
FocusScope {
    id: multiSelectRoot

    signal goBack()

    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    property string settingKey: navParams.settingKey || ""
    property string settingLabel: navParams.settingLabel || ""

    property var options: []         // [{id, label, default}], and [{label, heading}]
    property var enabledMap: ({})    // id -> bool, as the page shows them
    property var saved: ({})         // the row as saved, read as the choices come

    function buildEnabledMap() {
        var allSettings = appCore.get_settings()
        var moduleConfig = (allSettings.modules && allSettings.modules[moduleId]) ? allSettings.modules[moduleId] : {}
        saved = moduleConfig[settingKey] || {}
        var map = {}
        for (var i = 0; i < options.length; i++) {
            var option = options[i]
            if (!option.heading)
                map[option.id] = saved[option.id] !== undefined ? saved[option.id] : option.default !== false
        }
        enabledMap = map
    }

    // The ids of the choices under the heading at index.
    function groupOf(index) {
        var ids = []
        for (var i = index + 1; i < options.length && !options[i].heading; i++)
            ids.push(options[i].id)
        return ids
    }

    // A choice flipped, or a heading's group all on (off once they all are),
    // and the row saved.
    function toggleItem(index) {
        var option = options[index]
        if (!option)
            return
        var ids = option.heading ? groupOf(index) : [option.id]
        var on = !ids.every(function(id) { return enabledMap[id] })
        var shown = Object.assign({}, enabledMap)
        var kept = Object.assign({}, saved)
        for (var i = 0; i < ids.length; i++)
            shown[ids[i]] = kept[ids[i]] = on
        enabledMap = shown
        saved = kept
        appCore.save_setting(moduleId, settingKey, kept)
    }

    // Receive dynamic options from backend via appCore
    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            if (mid !== multiSelectRoot.moduleId || key !== multiSelectRoot.settingKey) return
            multiSelectRoot.options = items
            multiSelectRoot.buildEnabledMap()
        }
    }

    Component.onCompleted: {
        // Request the options list from the backend
        // Find the options_slot for this setting key
        // The setting may sit in a submenu ("type": "submenu").
        var find = function(list) {
            for (var i = 0; i < list.length; i++) {
                if (list[i].key === settingKey && list[i].options_slot) return list[i]
                var inner = list[i].type === "submenu" ? find(list[i].settings || []) : null
                if (inner) return inner
            }
            return null
        }
        var setting = find(appCore.get_module_settings_schema(moduleId))
        if (setting)
            appCore.invoke_module_action(moduleId, setting.options_slot)
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: moduleId.split(".").pop().toUpperCase() + " / " + settingLabel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // "NAME······ON", a heading "GENRES ─────" over its choices. A name too
    // long for its line is cut short rather than its ON or OFF.
    MenuList {
        id: optionsList
        model: multiSelectRoot.options
        focus: true

        delegate: MenuRow {
            required property var modelData
            required property int index
            width: optionsList.width
            height: root.sh * 0.0583333 //28
            label: modelData.label || ""
            heading: !!modelData.heading
            value: modelData.heading ? "" : (multiSelectRoot.enabledMap[modelData.id] ? "ON" : "OFF")
            keepValue: true
            selected: optionsList.currentIndex === index
        }

        Keys.onLeftPressed: multiSelectRoot.toggleItem(currentIndex)
        Keys.onRightPressed: multiSelectRoot.toggleItem(currentIndex)
        Keys.onReturnPressed: multiSelectRoot.toggleItem(currentIndex)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                multiSelectRoot.goBack()
                event.accepted = true
            }
        }
    }

    // What a heading does, on its line.
    HelpLine {
        readonly property var currentRow: multiSelectRoot.options[optionsList.currentIndex]
        visible: !!(currentRow && currentRow.heading)
        text: "Turns all of the choices under it on or off"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
