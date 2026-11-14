import QtQuick
import Components

// A module's text_list row (YouTube's Subscriptions, Weather's Other Places):
// the lines typed for it, one a row, and ADD under them. Select on ADD types a
// new line on the on-screen keyboard; select on a line asks whether to change
// it or take it out. The row is saved whole at every change, as a list of
// lines under its key, and its apply_slot told, as a "text" row's is.
FocusScope {
    id: textListRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal goBack()

    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    property string settingKey: navParams.settingKey || ""
    property string settingLabel: navParams.settingLabel || ""
    property string moduleName: navParams.moduleName || moduleId.split(".").pop().toUpperCase()
    property string help: navParams.help || ""
    property string applySlot: navParams.applySlot || ""
    property int maxLength: navParams.maxLength || 64
    // No more lines than this: the list goes on a QR code with the rest.
    property int maxLines: navParams.maxLines || 50

    property var lines: []
    // The line being typed over, or -1 for a new one.
    property int typingAt: -1
    readonly property bool full: lines.length >= maxLines

    function read() {
        var all = appCore.get_settings()
        var config = (all.modules && all.modules[moduleId]) ? all.modules[moduleId] : {}
        var saved = config[settingKey]
        var out = []
        if (saved && saved.length !== undefined && typeof saved !== "string") {
            for (var i = 0; i < saved.length; i++) {
                var line = String(saved[i]).trim()
                if (line !== "")
                    out.push(line)
            }
        }
        lines = out
    }

    // The list saved as it would be, and shown once it is, the cursor on the
    // row at `at` (a list given new rows starts again at its first): one that
    // couldn't be saved (a full card) leaves the page on what is saved.
    function keep(next, at) {
        if (!appCore.save_setting(moduleId, settingKey, next))
            return false
        lines = next
        list.currentIndex = Math.max(0, Math.min(at, next.length))
        if (applySlot !== "")
            appCore.invoke_module_action(moduleId, applySlot)
        return true
    }

    // A line typed: a new one at the foot, or over the one being changed.
    // Nothing typed takes the line out, and a line already there isn't
    // listed twice.
    function typed(text) {
        var line = text.trim()
        var next = lines.slice()
        if (typingAt >= 0)
            next.splice(typingAt, 1)
        if (line !== "" && next.indexOf(line) < 0) {
            if (typingAt >= 0)
                next.splice(typingAt, 0, line)
            else
                next.push(line)
        }
        // A new line leaves the cursor on ADD, for the next one.
        if (JSON.stringify(next) !== JSON.stringify(lines))
            keep(next, typingAt >= 0 ? typingAt : next.length)
    }

    function remove(index) {
        var next = lines.slice()
        next.splice(index, 1)
        keep(next, index)
    }

    function type(index) {
        typingAt = index
        keyboard.open(index >= 0 ? lines[index] : "")
    }

    Component.onCompleted: read()

    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: moduleName + " / " + settingLabel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The lines, then ADD: dimmed once the list is full.
    MenuList {
        id: list
        objectName: "lines"
        model: textListRoot.lines.concat([""])
        focus: true
        readonly property bool onAdd: currentIndex === count - 1

        delegate: MenuRow {
            required property var modelData
            required property int index
            readonly property bool add: index === list.count - 1
            width: list.width
            height: root.sh * 0.0583333 //28
            label: add ? "Add" : modelData
            opens: !add || !textListRoot.full
            dimmed: add && textListRoot.full
            selected: list.currentIndex === index
        }

        function open() {
            if (onAdd) {
                if (!textListRoot.full)
                    textListRoot.type(-1)
            } else {
                options.open()
            }
        }

        Keys.onRightPressed: open()
        Keys.onReturnPressed: open()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                textListRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        visible: text !== ""
        text: list.onAdd && textListRoot.full ? "The list is full: take a line out first" : textListRoot.help
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // What to do with a line.
    ChoiceOverlay {
        id: options
        objectName: "options"
        anchors.fill: parent
        promptText: textListRoot.settingLabel
        subtitleText: textListRoot.lines[list.currentIndex] || ""
        choices: [{ label: "Change", action: "change" }, { label: "Remove", action: "remove" }]
        onActivated: function(action) {
            if (action === "change")
                textListRoot.type(list.currentIndex)
            else
                textListRoot.remove(list.currentIndex)
        }
        onClosed: list.forceActiveFocus()
    }

    // Upper and lower case as typed: a channel's id, a place's name, a link.
    OnScreenKeyboard {
        id: keyboard
        objectName: "keyboard"
        anchors.fill: parent
        visible: false
        caseSensitive: true
        allowEmpty: true
        title: textListRoot.settingLabel
        maxLength: textListRoot.maxLength
        onAccepted: function(text) {
            keyboard.close()
            keyboard.text = ""
            list.forceActiveFocus()
            textListRoot.typed(text)
        }
        onCanceled: {
            keyboard.close()
            keyboard.text = ""
            list.forceActiveFocus()
        }
    }
}
