import QtQuick
import Components

// Settings → Bluetooth: pairs a Bluetooth keyboard, gamepad or remote with the
// Pi, through bluetoothManager (BlueZ). Laid out like Settings: BLUETOOTH on
// or off, SEARCH, then PAIRED and FOUND devices. SEARCH looks for devices for a
// minute, listing what answers as it does; select on one found pairs it, and a
// keyboard then has a code to type on it, which comes up over the list.
// Select on a paired one offers to connect, disconnect or forget it.
FocusScope {
    id: bluetoothRoot

    signal goBack()

    property var navParams: ({})

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Update.qml).
    readonly property bool available: bluetoothManager ? bluetoothManager.available : false
    readonly property bool powered: bluetoothManager ? bluetoothManager.powered : false
    readonly property bool searching: bluetoothManager ? bluetoothManager.searching : false
    readonly property var devices: bluetoothManager ? bluetoothManager.devices : []
    readonly property var prompt: bluetoothManager ? bluetoothManager.prompt : ({})
    readonly property string message: bluetoothManager ? bluetoothManager.message : ""
    readonly property bool powerFailed: bluetoothManager ? bluetoothManager.powerFailed : false

    // The lines: { type: "power" | "details" | "search" | "section" | "note" | "device", … }.
    property var rows: []
    // A device whose options are open.
    property var optionsDevice: null
    // The dots after SEARCHING, one more each half second.
    property int dots: 1

    onAvailableChanged: buildRows()
    onPoweredChanged: buildRows()
    onSearchingChanged: buildRows()
    onPowerFailedChanged: buildRows()
    onDevicesChanged: buildRows()
    Component.onCompleted: buildRows()

    // Leaving the page ends what it started: nothing would show a pairing's
    // code any more, and a search keeps the radio busy.
    Component.onDestruction: {
        if (bluetoothManager) {
            bluetoothManager.stopSearch()
            bluetoothManager.cancelPairing()
            bluetoothManager.clearMessage()
        }
    }

    function deviceValue(d) {
        if (d.busy === "pairing")       return "Pairing"
        if (d.busy === "connecting")    return "Connecting"
        if (d.busy === "disconnecting") return "Disconnecting"
        if (d.paired)                   return d.connected ? "Connected" : "Paired"
        return d.kind || "New"
    }

    function buildRows() {
        // The cursor stays on its line as lines come and go: a device by its
        // path (it moves from FOUND to PAIRED once paired), the others by type.
        var at = list.currentIndex
        var current = rows[at]
        var items = []
        if (!available) {
            items.push({ type: "note", label: "No Bluetooth",
                         description: "Bluetooth isn't running, or this computer has none" })
        } else {
            items.push({ type: "power", label: "Bluetooth", value: powered ? "On" : "Off",
                         description: "Turns Bluetooth on or off" })
            // It wouldn't turn on: what the system says about it.
            if (powerFailed && !powered)
                items.push({ type: "details", label: "Details",
                             description: "What the system says about Bluetooth. A photo of it helps find what is wrong" })
            items.push({ type: "search", label: "Search",
                         value: searching ? "Searching" : "", busy: searching,
                         description: searching
                             ? "Looking for devices for a minute. Select stops it"
                             : "Put the device in pairing mode (see its manual), then select SEARCH. Devices found are listed below" })
            var paired = devices.filter(function(d) { return d.paired })
            var found = devices.filter(function(d) { return !d.paired })
            if (paired.length > 0) {
                items.push({ type: "section", label: "Paired" })
                for (var i = 0; i < paired.length; i++)
                    items.push({ type: "device", label: paired[i].name, value: deviceValue(paired[i]),
                                 device: paired[i], busy: paired[i].busy !== "",
                                 description: "Select to connect, disconnect or forget it" })
            }
            if (found.length > 0 || searching) {
                items.push({ type: "section", label: "Found" })
                for (var j = 0; j < found.length; j++)
                    items.push({ type: "device", label: found[j].name, value: deviceValue(found[j]),
                                 device: found[j], busy: found[j].busy !== "",
                                 description: "Select to pair and connect it" })
                if (found.length === 0)
                    items.push({ type: "note", label: "Searching", busy: true,
                                 description: "Put the device in pairing mode, close by" })
            }
        }
        rows = items

        var index = -1
        if (current) {
            for (var k = 0; k < items.length && index < 0; k++) {
                if (current.type === "device" ? (items[k].type === "device" && items[k].device.path === current.device.path)
                                              : items[k].type === current.type && current.type !== "section" && current.type !== "note")
                    index = k
            }
        }
        // Its line gone (a device forgotten): the one now in its place.
        if (index < 0)
            index = selectableFrom(Math.min(Math.max(at, 0), items.length - 1), 1)
        list.currentIndex = index
        list.positionViewAtIndex(index, ListView.Contain)
    }

    function selectable(row) { return !!row && row.type !== "section" && row.type !== "note" }

    // The first line that can be chosen from index on, going by step, round.
    function selectableFrom(index, step) {
        for (var n = 0; n < rows.length; n++) {
            var i = ((index + n * step) % rows.length + rows.length) % rows.length
            if (selectable(rows[i]))
                return i
        }
        return 0
    }

    function activate(row) {
        if (!row || !bluetoothManager)
            return
        if (row.type === "power") {
            bluetoothManager.setPowered(!powered)
        } else if (row.type === "details") {
            bluetoothManager.collectDetails()
            details.open()
        } else if (row.type === "search") {
            if (searching)
                bluetoothManager.stopSearch()
            else
                bluetoothManager.startSearch()
        } else if (row.type === "device") {
            if (row.device.busy !== "")
                return
            if (!row.device.paired) {
                bluetoothManager.pair(row.device.path)
                return
            }
            optionsDevice = row.device
            options.choices = [
                row.device.connected ? { label: "Disconnect", action: "disconnect" }
                                     : { label: "Connect", action: "connect" },
                { label: "Forget", action: "forget" }
            ]
            options.subtitleText = row.device.name
            options.open()
        }
    }

    // A line's work under way (searching, pairing…) trails dots that come
    // and go.
    Timer {
        interval: 500
        repeat: true
        running: bluetoothRoot.rows.some(function(row) { return !!row.busy })
        onTriggered: bluetoothRoot.dots = bluetoothRoot.dots % 3 + 1
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/bluetooth.svg"
        title: "Bluetooth"
        subtitle: bluetoothManager ? bluetoothManager.adapterName : ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: list
        model: bluetoothRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        // Lines come and go as devices answer: no sliding.
        highlightMoveDuration: 0

        function move(step) {
            if (bluetoothRoot.rows.length === 0)
                return
            currentIndex = bluetoothRoot.selectableFrom(currentIndex + step, step)
            positionViewAtIndex(currentIndex, ListView.Contain)
            if (bluetoothManager)
                bluetoothManager.clearMessage()
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onLeftPressed: {
            var row = bluetoothRoot.rows[currentIndex]
            if (row && row.type === "power")
                bluetoothRoot.activate(row)
        }
        Keys.onRightPressed: {
            var row = bluetoothRoot.rows[currentIndex]
            if (row && row.type === "power")
                bluetoothRoot.activate(row)
        }
        Keys.onReturnPressed: bluetoothRoot.activate(bluetoothRoot.rows[currentIndex])
        Keys.onEnterPressed: bluetoothRoot.activate(bluetoothRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                bluetoothRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "KEYBOARD······CONNECTED", and
        // the groups' headings like Settings': "PAIRED ─────".
        delegate: MenuRow {
            readonly property string trail: modelData.busy ? ".".repeat(bluetoothRoot.dots) : ""
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: (modelData.label || "") + (modelData.type === "note" ? trail : "")
            value: modelData.value ? modelData.value + trail : ""
            selected: list.currentIndex === index && bluetoothRoot.selectable(modelData)
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: list
        list: list
    }

    // What the line is for, or how the last pairing went.
    HelpLine {
        property var currentRow: bluetoothRoot.rows[list.currentIndex]
        text: bluetoothRoot.message !== "" ? bluetoothRoot.message
              : ((currentRow && currentRow.description) || "")
        visible: text !== ""
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

    // A paired device's options.
    ChoiceOverlay {
        id: options
        anchors.fill: parent
        promptText: "Bluetooth"
        onActivated: function(action) {
            var device = bluetoothRoot.optionsDevice
            if (!device || !bluetoothManager)
                return
            if (action === "connect")
                bluetoothManager.connectDevice(device.path)
            else if (action === "disconnect")
                bluetoothManager.disconnectDevice(device.path)
            else if (action === "forget")
                bluetoothManager.forget(device.path)
        }
        onClosed: list.forceActiveFocus()
    }

    // What the system says about Bluetooth, for a photo: ▲ ▼ scroll it.
    FocusScope {
        id: details
        anchors.fill: parent
        visible: false
        // Hidden, it lets go of the keys: an item only hidden would keep them.
        enabled: visible

        function open() {
            visible = true
            detailsText.contentY = 0
            forceActiveFocus()
        }
        function close() {
            visible = false
            list.forceActiveFocus()
        }

        Keys.onPressed: function(event) {
            // Ctrl+Q and the rest go on to Main.qml.
            if (event.modifiers & Qt.ControlModifier)
                return
            event.accepted = true
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
                    || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                close()
            else if (event.key === Qt.Key_Up)
                detailsText.scrollBy(-1)
            else if (event.key === Qt.Key_Down)
                detailsText.scrollBy(1)
        }

        OsdGround {
            anchors.fill: parent
        }

        AppBar {
            iconSource: "../../assets/images/bluetooth.svg"
            title: "Bluetooth"
            subtitle: "Details"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.125 //60
            anchors.leftMargin: root.sw * 0.125 //80
        }

        Flickable {
            id: detailsText
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.2166667 //104
            anchors.leftMargin: root.sw * 0.125 //80
            width: root.sw * 0.75 //480
            height: root.sh * 0.5 + root.hintRoom //240
            clip: true
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            // A line's worth at a time.
            function scrollBy(step) {
                var line = body.font.pixelSize * 1.2
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + step * line * 3))
            }

            Text {
                id: body
                width: detailsText.width
                text: (bluetoothManager && bluetoothManager.details) || "…"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667 //14
                wrapMode: Text.WrapAnywhere
                lineHeight: 1.2
            }
        }

        ScrollMarks {
            anchors.fill: detailsText
            list: detailsText
        }

        HintBar {
            text: root.hints.back + ":BACK " + root.hints.navigate + ":SCROLL"
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.bottomMargin: root.sh * 0.1041667 //50
            anchors.leftMargin: root.sw * 0.125 //80
        }
    }

    // What pairing needs: a code to type on the device, or to compare.
    BluetoothPrompt {
        anchors.fill: parent
        prompt: bluetoothRoot.prompt
        onAnswered: function(accept) {
            if (bluetoothManager)
                bluetoothManager.answerPrompt(accept)
        }
        onCanceled: {
            if (bluetoothManager)
                bluetoothManager.cancelPairing()
        }
        // The keys back to what it came over: a device's options, if open.
        onVisibleChanged: if (!visible) (options.visible ? options : list).forceActiveFocus()
    }
}
